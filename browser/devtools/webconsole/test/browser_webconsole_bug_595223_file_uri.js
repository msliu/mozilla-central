/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TEST_FILE = "test-network.html";

function tabReload(aEvent) {
  browser.removeEventListener(aEvent.type, tabReload, true);

  outputNode = hud.outputNode;
  findLogEntry("test-network.html");
  findLogEntry("test-image.png");
  findLogEntry("testscript.js");
  isnot(outputNode.textContent.indexOf("running network console logging tests"), -1,
        "found the console.log() message from testscript.js");

  executeSoon(finishTest);
}

function test() {
  let jar = getJar(getRootDirectory(gTestPath));
  let dir = jar ?
            extractJarToTmp(jar) :
            getChromeDir(getResolvedURI(gTestPath));
  dir.append(TEST_FILE);

  let uri = Services.io.newFileURI(dir);

  addTab(uri.spec);
  browser.addEventListener("load", function tabLoad() {
    browser.removeEventListener("load", tabLoad, true);
    openConsole(null, function(aHud) {
      hud = aHud;
      browser.addEventListener("load", tabReload, true);
      content.location.reload();
    });
  }, true);
}
