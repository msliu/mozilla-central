/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */


//
// Mike Pinkerton
// Netscape Communications
//
// See associated header file for details
//


#include "nsDragService.h"

#include "nsITransferable.h"
#include "nsString.h"
#include "nsMimeMapper.h"
#include "nsClipboard.h"
#include "nsIRegion.h"
#include "nsVoidArray.h"
#include "nsISupportsPrimitives.h"
#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsPrimitiveHelpers.h"


DragSendDataUPP nsDragService::sDragSendDataUPP = NewDragSendDataProc(DragSendDataProc);

// we need our own stuff for MacOS because of nsIDragSessionMac.
NS_IMPL_ADDREF_INHERITED(nsDragService, nsBaseDragService)
NS_IMPL_RELEASE_INHERITED(nsDragService, nsBaseDragService)
NS_IMPL_QUERY_INTERFACE3(nsDragService, nsIDragService, nsIDragSession, nsIDragSessionMac)


//
// DragService constructor
//
nsDragService::nsDragService()
  : mDragRef(0), mDataItems(nsnull)
{
}


//
// DragService destructor
//
nsDragService::~nsDragService()
{
}


//
// StartDragSession
//
// Do all the work to kick it off.
//
NS_IMETHODIMP
nsDragService :: InvokeDragSession (nsISupportsArray * aTransferableArray, nsIScriptableRegion * aDragRgn, PRUint32 aActionType)
{
  DragReference theDragRef;
  OSErr result = ::NewDrag(&theDragRef);
  if ( result != noErr )
    return NS_ERROR_FAILURE;
  mDragRef = theDragRef;
#if DEBUG_DD
printf("**** created drag ref %ld\n", theDragRef);
#endif
  
  // add the flavors from the transferables. Cache this array for the send data proc
  mDataItems = aTransferableArray;
  RegisterDragItemsAndFlavors ( aTransferableArray ) ;
  
  // we have to synthesize the native event because we may be called from JavaScript
  // through XPConnect. In that case, we only have a DOM event and no way to
  // get to the native event. As a consequence, we just always fake it.
  Point globalMouseLoc;
  ::GetMouse(&globalMouseLoc);
  ::LocalToGlobal(&globalMouseLoc);
  WindowPtr theWindow = nsnull;
  if ( ::FindWindow(globalMouseLoc, &theWindow) != inContent ) {
    // debugging sanity check
    #ifdef NS_DEBUG
    DebugStr("\pAbout to start drag, but FindWindow() != inContent; g");
    #endif
  }  
  EventRecord theEvent;
  theEvent.what = mouseDown;
  theEvent.message = reinterpret_cast<UInt32>(theWindow);
  theEvent.when = 0;
  theEvent.where = globalMouseLoc;
  theEvent.modifiers = 0;
  
  RgnHandle theDragRgn = ::NewRgn();
  BuildDragRegion ( aDragRgn, globalMouseLoc, theDragRgn );

  // register drag send proc which will call us back when asked for the actual
  // flavor data (instead of placing it all into the drag manager)
  ::SetDragSendProc ( theDragRef, sDragSendDataUPP, this );

  // start the drag. Be careful, mDragRef will be invalid AFTER this call (it is
  // reset by the dragTrackingHandler).
  StartDragSession();
  ::TrackDrag ( theDragRef, &theEvent, theDragRgn );
  EndDragSession();
  
  // clean up after ourselves 
  ::DisposeRgn ( theDragRgn );
  result = ::DisposeDrag ( theDragRef );
#if DEBUG_DD
printf("**** disposing drag ref %ld\n", theDragRef);
#endif
  NS_ASSERTION ( result == noErr, "Error disposing drag" );
  mDragRef = 0L;
  mDataItems = nsnull;

  return NS_OK; 

} // StartDragSession


//
// BuildDragRegion
//
// Given the XP region describing the drag rectangles, build up an appropriate drag region. If
// the region we're given is null, create our own placeholder.
//
void
nsDragService :: BuildDragRegion ( nsIScriptableRegion* inRegion, Point inGlobalMouseLoc, RgnHandle ioDragRgn )
{
  nsCOMPtr<nsIRegion> geckoRegion ( do_QueryInterface(inRegion) );
    
  // create the drag region. Pull out the native mac region from the nsIRegion we're
  // given, copy it, inset it one pixel, and subtract them so we're left with just an
  // outline. Too bad we can't do this with gfx api's.
  //
  // At the end, we are left with an outline of the region in global coordinates.
  if ( geckoRegion ) {
    RgnHandle dragRegion = nsnull;
    geckoRegion->GetNativeRegion(dragRegion);
    if ( dragRegion && ioDragRgn ) {
      ::CopyRgn ( dragRegion, ioDragRgn );
      ::InsetRgn ( ioDragRgn, 1, 1 );
      ::DiffRgn ( dragRegion, ioDragRgn, ioDragRgn ); 

      // now shift the region into global coordinates.
      Point offsetFromLocalToGlobal = { 0, 0 };
      ::LocalToGlobal ( &offsetFromLocalToGlobal );
      ::OffsetRgn ( ioDragRgn, offsetFromLocalToGlobal.h, offsetFromLocalToGlobal.v );
    }
  }
  else {
    // no region provided, so we create a default one that is 50 x 50, with the topLeft
    // being at the cursor location.
    NS_WARNING ( "you MUST pass a drag region for MacOS. This is a warning" );    
    if ( ioDragRgn ) {
      Rect placeHolderRect = { inGlobalMouseLoc.v, inGlobalMouseLoc.h, inGlobalMouseLoc.v + 50, 
                                inGlobalMouseLoc.h + 50 };
      RgnHandle placeHolderRgn = ::NewRgn();
      if ( placeHolderRgn ) {
        ::RectRgn ( placeHolderRgn, &placeHolderRect );
        ::CopyRgn ( placeHolderRgn, ioDragRgn );
        ::InsetRgn ( ioDragRgn, 1, 1 );
        ::DiffRgn ( placeHolderRgn, ioDragRgn, ioDragRgn );
        
        ::DisposeRgn ( placeHolderRgn );
      }
    }
  }

} // BuildDragRegion


//
// RegisterDragItemsAndFlavors
//
// Takes the multiple drag items from an array of transferables and registers them
// and their flavors with the MacOS DragManager. Note that we don't actually place
// any of the data there yet, but will rely on a sendDataProc to get the data as
// requested.
//
void
nsDragService :: RegisterDragItemsAndFlavors ( nsISupportsArray * inArray )
{
  const FlavorFlags flags = 0;
  
  unsigned int numDragItems = 0;
  inArray->Count ( &numDragItems ) ;
  for ( int itemIndex = 0; itemIndex < numDragItems; ++itemIndex ) {
    nsMimeMapperMac theMapper;
  
    nsCOMPtr<nsISupports> genericItem;
    inArray->GetElementAt ( itemIndex, getter_AddRefs(genericItem) );
    nsCOMPtr<nsITransferable> currItem ( do_QueryInterface(genericItem) );
    if ( currItem ) {   
      nsCOMPtr<nsISupportsArray> flavorList;
      if ( NS_SUCCEEDED(currItem->FlavorsTransferableCanExport(getter_AddRefs(flavorList))) ) {
        PRUint32 numFlavors;
        flavorList->Count ( &numFlavors );
        for ( int flavorIndex = 0; flavorIndex < numFlavors; ++flavorIndex ) { 
        
          nsCOMPtr<nsISupports> genericWrapper;
          flavorList->GetElementAt ( flavorIndex, getter_AddRefs(genericWrapper) );
          nsCOMPtr<nsISupportsString> currentFlavor ( do_QueryInterface(genericWrapper) );
	      if ( currentFlavor ) {
	        nsXPIDLCString flavorStr;
	        currentFlavor->ToString ( getter_Copies(flavorStr) );
	        FlavorType macOSFlavor = theMapper.MapMimeTypeToMacOSType(flavorStr);
	        ::AddDragItemFlavor ( mDragRef, itemIndex, macOSFlavor, NULL, 0, flags );
	        
	        // If we advertise text/unicode, then make sure we add 'TEXT' to the list
	        // of flavors supported since we will do the conversion ourselves in GetDataForFlavor()
	        if ( strcmp(flavorStr, kUnicodeMime) == 0 ) {
	          theMapper.MapMimeTypeToMacOSType(kTextMime);
	          ::AddDragItemFlavor ( mDragRef, itemIndex, 'TEXT', NULL, 0, flags );	        
	        }
	      }
          
        } // foreach flavor in item              
      } // if valid flavor list
    } // if item is a transferable
    
    // put the mime mapping data for this item in a special flavor. Unlike the other data,
    // we have to put the data in now (rather than defer it) or the mappings will go out 
    // of scope by the time they are asked for. Remember that the |mappingLen|
    // includes the null.
    short mappingLen;
    char* mapping = theMapper.ExportMapping(&mappingLen);
    if ( mapping && mappingLen ) {
      ::AddDragItemFlavor ( mDragRef, itemIndex, nsMimeMapperMac::MappingFlavor(), 
                               mapping, mappingLen - 1, flags );
	  nsCRT::free ( mapping );
	}
    
  } // foreach drag item 

} // RegisterDragItemsAndFlavors


//
// GetData
//
// Pull data out of the  OS drag item at the requested index and stash it into the 
// given transferable. Only put in the data with the highest fidelity asked for and
// stop as soon as we find a match.
//
NS_IMETHODIMP
nsDragService :: GetData ( nsITransferable * aTransferable, PRUint32 aItemIndex )
{
  nsresult errCode = NS_ERROR_FAILURE;

  // make sure we have a good transferable
  if ( !aTransferable )
    return NS_ERROR_INVALID_ARG;

  // get flavor list that includes all acceptable flavors (including ones obtained through
  // conversion). Flavors are nsISupportsStrings so that they can be seen from JS.
  nsCOMPtr<nsISupportsArray> flavorList;
  errCode = aTransferable->FlavorsTransferableCanImport ( getter_AddRefs(flavorList) );
  if ( NS_FAILED(errCode) )
    return errCode;

  // get the data for the requested drag item. Remember that GetDragItemReferenceNumber()
  // is one-based NOT zero-based like |aItemIndex| is.   
  ItemReference itemRef;
  ::GetDragItemReferenceNumber ( mDragRef, aItemIndex + 1, &itemRef );
 
  // create a mime mapper to help us out based on data in a special flavor for this item
  char* mappings = LookupMimeMappingsForItem(mDragRef, itemRef);
  nsMimeMapperMac theMapper ( mappings );
  nsCRT::free ( mappings );
  
  // Now walk down the list of flavors. When we find one that is actually present,
  // copy out the data into the transferable in that format. SetTransferData()
  // implicitly handles conversions.
  PRUint32 cnt;
  flavorList->Count ( &cnt );
  for ( int i = 0; i < cnt; ++i ) {
    nsCOMPtr<nsISupports> genericWrapper;
    flavorList->GetElementAt ( i, getter_AddRefs(genericWrapper) );
    nsCOMPtr<nsISupportsString> currentFlavor ( do_QueryInterface(genericWrapper) );
    if ( currentFlavor ) {
      // find MacOS flavor (but don't add it if it's not there)
      nsXPIDLCString flavorStr;
      currentFlavor->ToString ( getter_Copies(flavorStr) );
      FlavorType macOSFlavor = theMapper.MapMimeTypeToMacOSType(flavorStr, PR_FALSE);
#if DEBUG_DD
printf("looking for data in type %s, mac flavor %ld\n", NS_STATIC_CAST(const char*,flavorStr), macOSFlavor);
#endif

      // check if it is present in the current drag item.
      FlavorFlags unused;
      PRBool dataFound = PR_FALSE;
	  void* dataBuff;
      PRInt32 dataSize = 0;
      if ( macOSFlavor && ::GetFlavorFlags(mDragRef, itemRef, macOSFlavor, &unused) == noErr ) {	    
        nsresult loadResult = ExtractDataFromOS(mDragRef, itemRef, macOSFlavor, &dataBuff, &dataSize);
	    if ( NS_SUCCEEDED(loadResult) && dataBuff )
	      dataFound = PR_TRUE;
      }
      else {
	    // if we are looking for text/unicode and we fail to find it on the clipboard first,
        // try again with text/plain. If that is present, convert it to unicode.
        if ( strcmp(flavorStr, kUnicodeMime) == 0 ) {
          if ( ::GetFlavorFlags(mDragRef, itemRef, 'TEXT', &unused) == noErr ) {	    
            nsresult loadResult = ExtractDataFromOS(mDragRef, itemRef, 'TEXT', &dataBuff, &dataSize);
            if ( NS_SUCCEEDED(loadResult) && dataBuff ) {
              const char* castedText = NS_REINTERPRET_CAST(char*, dataBuff);          
              PRUnichar* convertedText = nsnull;
              PRInt32 convertedTextLen = 0;
              nsPrimitiveHelpers::ConvertPlatformPlainTextToUnicode ( castedText, dataSize, 
                                                                        &convertedText, &convertedTextLen );
              if ( convertedText ) {
                // out with the old, in with the new 
                nsAllocator::Free(dataBuff);
                dataBuff = convertedText;
                dataSize = convertedTextLen * 2;
                dataFound = PR_TRUE;
              }
            } // if plain text data on clipboard
          } // if plain text flavor present
        } // if looking for text/unicode   
      } // else we try one last ditch effort to find our data

	  if ( dataFound ) {
        // the DOM only wants LF, so convert from MacOS line endings to DOM line
        // endings.
        nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks ( flavorStr, &dataBuff, NS_REINTERPRET_CAST(int*, &dataSize) );
        
        // put it into the transferable.
        nsCOMPtr<nsISupports> genericDataWrapper;
        nsPrimitiveHelpers::CreatePrimitiveForData ( flavorStr, dataBuff, dataSize, getter_AddRefs(genericDataWrapper) );
        errCode = aTransferable->SetTransferData ( flavorStr, genericDataWrapper, dataSize );
        #ifdef NS_DEBUG
         if ( errCode != NS_OK ) printf("nsDragService:: Error setting data into transferable\n");
        #endif
          
        nsAllocator::Free ( dataBuff );
        errCode = NS_OK;

        // we found one, get out of this loop!
        break;
      } 
    }
  } // foreach flavor
  
  return errCode;
}




//
// IsDataFlavorSupported
//
// Check the OS to see if the given drag flavor is in the list. Oddly returns
// NS_OK for success and NS_ERROR_FAILURE if flavor is not present.
//
// Handle the case where we ask for unicode and it's not there, but plain text is. We 
// say "yes" in that case, knowing that we will have to perform a conversion when we actually
// pull the data out of the drag.
//
// ��� this is obviously useless with more than one drag item. Need to specify 
// ����and index to this API
//
NS_IMETHODIMP
nsDragService :: IsDataFlavorSupported(const char *aDataFlavor, PRBool *_retval)
{
  if ( !_retval )
    return NS_ERROR_INVALID_ARG;

#ifdef NS_DEBUG
      if ( strcmp(aDataFlavor, kTextMime) == 0 )
        NS_WARNING ( "DO NOT USE THE text/plain DATA FLAVOR ANY MORE. USE text/unicode INSTEAD" );
#endif

  *_retval = PR_FALSE;

  // convert to 4 character MacOS type
  //��� this is wrong because it doesn't take the mime mappings present in the
  //��� drag item flavor into account. FIX ME!
  nsMimeMapperMac theMapper;
  FlavorType macFlavor = theMapper.MapMimeTypeToMacOSType(aDataFlavor);

  // search through all drag items looking for something with this flavor. Recall
  // that drag item indices are 1-based.
  unsigned short numDragItems = 0;
  ::CountDragItems ( mDragRef, &numDragItems );
  for ( int i = 1; i <= numDragItems; ++i ) {
    ItemReference currItem;
    OSErr res = ::GetDragItemReferenceNumber ( mDragRef, i, &currItem );
    if ( res != noErr )
      return NS_ERROR_FAILURE;

    FlavorFlags ignored;
    if ( ::GetFlavorFlags(mDragRef, currItem, macFlavor, &ignored) == noErr )
      *_retval = PR_TRUE;
    else {
      // if the client asked for unicode and it wasn't present, check if we have TEXT.
      // We'll handle the actual data substitution in GetDataForFlavor().
      if ( strcmp(aDataFlavor, kUnicodeMime) == 0 ) {
        if ( ::GetFlavorFlags(mDragRef, currItem, 'TEXT', &ignored) == noErr )
          *_retval = PR_TRUE;
      }          
    }
  } // for each item in drag

  return NS_OK;

} // IsDataFlavorSupported


//
// GetNumDropItems
//
// Returns the number of drop items present in the current drag.
//
NS_IMETHODIMP
nsDragService :: GetNumDropItems ( PRUint32 * aNumItems )
{
  // we have to put it into a short first because that's what the MacOS API's expect.
  // After it's in a short, getting it into a long is no problem. Oh well.
  unsigned short numDragItems = 0;
  OSErr result = ::CountDragItems ( mDragRef, &numDragItems );
  *aNumItems = numDragItems;
  
  return ( result == noErr ? NS_OK : NS_ERROR_FAILURE );

} // GetNumDropItems


//
// SetDragReference
//
// An API to allow the drag manager callback functions to tell the session about the
// current dragRef w/out resorting to knowing the internals of the implementation
//
NS_IMETHODIMP
nsDragService :: SetDragReference ( DragReference aDragRef )
{
  mDragRef = aDragRef;
  return NS_OK;
  
} // SetDragReference


//
// DragSendDataProc
//
// Called when a drop occurs and the drop site asks for the data.
//
// This will only get called when Mozilla is the originator of the drag, so we can
// make certain assumptions. One is that we've cached the list of transferables in the
// drag session. The other is that the item ref is the index into this list so we
// can easily pull out the item asked for.
//
// We cannot rely on |mDragRef| at this point so we have to use what was passed in
// and pass that along.
// 
pascal OSErr
nsDragService :: DragSendDataProc ( FlavorType inFlavor, void* inRefCon, ItemReference inItemRef,
									DragReference inDragRef )
{
  OSErr retVal = noErr;
  nsDragService* self = NS_STATIC_CAST(nsDragService*, inRefCon);
  NS_ASSERTION ( self, "Refcon not set correctly for DragSendDataProc" );
  if ( self ) {
    void* data = nsnull;
    PRUint32 dataSize = 0;
    retVal = self->GetDataForFlavor ( self->mDataItems, inDragRef, inItemRef, inFlavor, &data, &dataSize );
    if ( retVal == noErr ) {      
        // make the data accessable to the DragManager
        retVal = ::SetDragItemFlavorData ( inDragRef, inItemRef, inFlavor, data, dataSize, 0 );
        NS_ASSERTION ( retVal == noErr, "SDIFD failed in DragSendDataProc" );
    }
    nsAllocator::Free ( data );
  } // if valid refcon
  
  return retVal;
  
} // DragSendDataProc


//
// GetDataForFlavor
//
// Given a MacOS flavor and an index for which drag item to lookup, get the information from the
// drag item corresponding to this flavor.
// 
// This routine also handles the conversions between text/plain and text/unicode to take platform
// charset encodings into account. If someone asks for text/plain, we convert the unicode (we assume
// it is present because that's how we knew to advertise text/plain in the first place) and give it
// to them. If someone asks for text/unicode, and it isn't there, we need to convert text/plain and
// hand them back the unicode. Again, we can assume that text/plain is there because otherwise we
// wouldn't have been allowed to look for unicode.
//
OSErr
nsDragService :: GetDataForFlavor ( nsISupportsArray* inDragItems, DragReference inDragRef, unsigned int inItemIndex, 
                                      FlavorType inFlavor, void** outData, unsigned int* outDataSize )
{
  if ( !inDragItems || !inDragRef )
    return paramErr;
    
  OSErr retVal = noErr;
  
  // (assumes that the items were placed into the transferable as nsITranferable*'s, not nsISupports*'s.)
  nsCOMPtr<nsISupports> genericItem;
  inDragItems->GetElementAt ( inItemIndex, getter_AddRefs(genericItem) );
  nsCOMPtr<nsITransferable> item ( do_QueryInterface(genericItem) );
  if ( item ) {
    nsCAutoString mimeFlavor;
    
    // create a mime mapper to help us out based on data in a special flavor for this item.
    char* mappings = LookupMimeMappingsForItem(inDragRef, inItemIndex) ;
    nsMimeMapperMac theMapper ( mappings );
    theMapper.MapMacOSTypeToMimeType ( inFlavor, mimeFlavor );
    nsAllocator::Free ( mappings );
    
    // if someone was asking for text/plain, lookup unicode instead so we can convert it.
    PRBool needToDoConversionToPlainText = PR_FALSE;
    char* actualFlavor = mimeFlavor;
    if ( strcmp(mimeFlavor,kTextMime) == 0 ) {
      actualFlavor = kUnicodeMime;
      needToDoConversionToPlainText = PR_TRUE;
    }
    else
      actualFlavor = mimeFlavor;
      
    *outDataSize = 0;
    nsCOMPtr<nsISupports> data;
    if ( NS_SUCCEEDED(item->GetTransferData(actualFlavor, getter_AddRefs(data), outDataSize)) ) {
      nsPrimitiveHelpers::CreateDataFromPrimitive ( actualFlavor, data, outData, *outDataSize );
      
      // if required, do the extra work to convert unicode to plain text and replace the output
      // values with the plain text.
      if ( needToDoConversionToPlainText ) {
        char* plainTextData = nsnull;
        PRUnichar* castedUnicode = NS_REINTERPRET_CAST(PRUnichar*, *outData);
        PRInt32 plainTextLen = 0;
        nsPrimitiveHelpers::ConvertUnicodeToPlatformPlainText ( castedUnicode, *outDataSize / 2, &plainTextData, &plainTextLen );
        if ( *outData ) {
          nsAllocator::Free(*outData);
          *outData = plainTextData;
          *outDataSize = plainTextLen;
        }
        else
          retVal = cantGetFlavorErr;
      }
    }
    else
      retVal = cantGetFlavorErr;
  } // if valid item

  return retVal;

} // GetDataForFlavor


//
// LookupMimeMappingsForItem
//
// load up the flavor data for the mime mapper from the drag item and create a
// mime mapper to help us go between mime types and macOS flavors. It may not be
// there (such as when the drag originated from another app), but that's ok.
//
// Caller is responsible for deleting the memory.
//
char*
nsDragService :: LookupMimeMappingsForItem ( DragReference inDragRef, ItemReference inItemRef )
{
  char* mapperData = nsnull;
  PRInt32 mapperSize = 0;
  ExtractDataFromOS(inDragRef, inItemRef, nsMimeMapperMac::MappingFlavor(),  &mapperData, &mapperSize);

  return mapperData;
  
#if 0 
  OSErr err = ::GetFlavorDataSize ( inDragRef, itemRef, nsMimeMapperMac::MappingFlavor(), &mapperSize );
  if ( !err && mapperSize > 0 ) {
    mapperData = NS_REINTERPRET_CAST(char*, nsAllocator::Alloc(mapperSize + 1));
    if ( !mapperData )
      return nsnull;
	          
    err = ::GetFlavorData ( inDragRef, itemRef, nsMimeMapperMac::MappingFlavor(), mapperData, &mapperSize, 0 );
    if ( err ) {
      #ifdef NS_DEBUG
        printf("nsDragService: Error getting data out of drag manager for mime mapper, #%ld\n", err);
      #endif
      return nsnull;
    }
    else
      mapperData[mapperSize] = '\0';    // null terminate the data
  }

  return mapperData; 
#endif
}


//
// ExtractDataFromOS
//
// Handles pulling the data from the DragManager. Will return NS_ERROR_FAILURE if it can't get
// the data for whatever reason.
//
nsresult
nsDragService :: ExtractDataFromOS ( DragReference inDragRef, ItemReference inItemRef, ResType inFlavor, 
                                        void** outBuffer, PRInt32* outBuffSize )
{
  if ( !outBuffer || !outBuffSize || !inFlavor )
    return NS_ERROR_FAILURE;

  nsresult retval = NS_OK;
  char* buff = nsnull;
  Size buffSize = 0;
  OSErr err = ::GetFlavorDataSize ( inDragRef, inItemRef, inFlavor, &buffSize );
  if ( !err && buffSize > 0 ) {
    buff = NS_REINTERPRET_CAST(char*, nsAllocator::Alloc(buffSize + 1));
    if ( buff ) {	     
      err = ::GetFlavorData ( inDragRef, inItemRef, inFlavor, buff, &buffSize, 0 );
      if ( err ) {
        #ifdef NS_DEBUG
          printf("nsDragService: Error getting data out of drag manager, #%ld\n", err);
        #endif
        retval = NS_ERROR_FAILURE;
      }
    }
    else
      retval = NS_ERROR_FAILURE;
  }

  if ( NS_FAILED(retval) ) {
    if ( buff )
      nsAllocator::Free(buff);
  }
  else {
    *outBuffer = buff;
    *outBuffSize = buffSize;
  }
  return retval;

} // ExtractDataFromOS
