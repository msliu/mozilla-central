/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code mozilla.org code.
 *
 * The Initial Developer of the Original Code Christopher Blizzard
 * <blizzard@mozilla.org>.  Portions created by the Initial Developer
 * are Copyright (C) 2001 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef __nsCommonWidget_h__
#define __nsCommonWidget_h__

#include "nsBaseWidget.h"
#include "nsGUIEvent.h"
#include <gdk/gdkevents.h>

class nsCommonWidget : public nsBaseWidget {
 public:
  nsCommonWidget();
  virtual ~nsCommonWidget();

  virtual nsIWidget *GetParent(void);

  void CommonCreate(nsIWidget *aParent);

  // event handling code
  void InitPaintEvent(nsPaintEvent &aEvent);
  void InitSizeEvent(nsSizeEvent &aEvent);
  void InitGUIEvent(nsGUIEvent &aEvent, PRUint32 aMsg);
  void InitMouseEvent(nsMouseEvent &aEvent, PRUint32 aMsg);
  void InitButtonEvent(nsMouseEvent &aEvent, PRUint32 aMsg,
		       GdkEventButton *aEvent);
  void InitMouseScrollEvent(nsMouseScrollEvent &aEvent,
			    GdkEventScroll *aGdkEvent, PRUint32 aMsg);
  void InitKeyEvent(nsKeyEvent &aEvent, GdkEventKey *aEvent, PRUint32 aMsg);

  void DispatchGotFocusEvent(void);
  void DispatchLostFocusEvent(void);
  void DispatchActivateEvent(void);
  void DispatchDeactivateEvent(void);
  
  NS_IMETHOD DispatchEvent(nsGUIEvent *aEvent,
			   nsEventStatus &aStatus);

  // called when we are destroyed
  void OnDestroy(void);

  // convert a key code into a DOM code
  static int ConvertKeyCodeToDOMKeyCode(int aKeysym);

 protected:
  nsCOMPtr<nsIWidget> mParent;
  PRPackedBool        mOnDestroyCalled;
};

#endif /* __nsCommonWidget_h__ */
