Cu.import("resource://services-sync/main.js");
Cu.import("resource://services-sync/util.js");
Cu.import("resource://services-sync/status.js");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/base_records/wbo.js");      // For Records.
Cu.import("resource://services-sync/base_records/crypto.js");   // For CollectionKeys.
Cu.import("resource://services-sync/log4moz.js");

function run_test() {
  if (DISABLE_TESTS_BUG_604565)
    return;

  let logger = Log4Moz.repository.rootLogger;
  Log4Moz.repository.rootLogger.addAppender(new Log4Moz.DumpAppender());

  let guidSvc = new FakeGUIDService();
  let clients = new ServerCollection();
  let meta_global = new ServerWBO('global');

  let collections = {};
  function info_collections(request, response) {
    let body = JSON.stringify(collections);
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.bodyOutputStream.write(body, body.length);
  }

  function wasCalledHandler(wbo) {
    let handler = wbo.handler();
    return function() {
      wbo.wasCalled = true;
      handler.apply(this, arguments);
    };
  }

  do_test_pending();
  let server = httpd_setup({
    "/1.0/johndoe/storage/crypto/keys": new ServerWBO().handler(),
    "/1.0/johndoe/storage/clients": clients.handler(),
    "/1.0/johndoe/storage/meta/global": wasCalledHandler(meta_global),
    "/1.0/johndoe/info/collections": info_collections
  });

  try {
    _("Log in.");
    Weave.Service.serverURL = "http://localhost:8080/";
    Weave.Service.clusterURL = "http://localhost:8080/";
    
    _("Checking Status.sync with no credentials.");
    Weave.Service.verifyAndFetchSymmetricKeys();
    do_check_eq(Status.sync, CREDENTIALS_CHANGED);
    do_check_eq(Status.login, LOGIN_FAILED_INVALID_PASSPHRASE);

    Weave.Service.login("johndoe", "ilovejane", "foo");
    do_check_true(Weave.Service.isLoggedIn);

    _("Checking that remoteSetup returns true when credentials have changed.");
    Records.get(Weave.Service.metaURL).payload.syncID = "foobar";
    do_check_true(Weave.Service._remoteSetup());
    
    _("Do an initial sync.");
    let beforeSync = Date.now()/1000;
    Weave.Service.sync();

    _("Checking that remoteSetup returns true.");
    do_check_true(Weave.Service._remoteSetup());

    _("Verify that the meta record was uploaded.");
    do_check_eq(meta_global.data.syncID, Weave.Service.syncID);
    do_check_eq(meta_global.data.storageVersion, Weave.STORAGE_VERSION);
    do_check_eq(meta_global.data.engines.clients.version, Weave.Clients.version);
    do_check_eq(meta_global.data.engines.clients.syncID, Weave.Clients.syncID);

    _("Set the collection info hash so that sync() will remember the modified times for future runs.");
    collections = {meta: Weave.Clients.lastSync,
                   clients: Weave.Clients.lastSync};
    Weave.Service.sync();

    _("Sync again and verify that meta/global wasn't downloaded again");
    meta_global.wasCalled = false;
    Weave.Service.sync();
    do_check_false(meta_global.wasCalled);

    _("Fake modified records. This will cause a redownload, but not reupload since it hasn't changed.");
    collections.meta += 42;
    meta_global.wasCalled = false;

    let metaModified = meta_global.modified;

    Weave.Service.sync();
    do_check_true(meta_global.wasCalled);
    do_check_eq(metaModified, meta_global.modified);

    _("Checking bad passphrases.");
    let pp = Weave.Service.passphrase;
    Weave.Service.passphrase = "notvalid";
    do_check_false(Weave.Service.verifyAndFetchSymmetricKeys());
    do_check_eq(Status.sync, CREDENTIALS_CHANGED);
    do_check_eq(Status.login, LOGIN_FAILED_INVALID_PASSPHRASE);
    Weave.Service.passphrase = pp;
    do_check_true(Weave.Service.verifyAndFetchSymmetricKeys());
    
    // Try to screw up HMAC calculation.
    // Re-encrypt keys with a new random keybundle, and upload them to the
    // server, just as might happen with a second client.
    _("Attempting to screw up HMAC by re-encrypting keys.");
    let keys = CollectionKeys.asWBO();
    let b = new BulkKeyBundle();
    b.generateRandom();
    collections.crypto = keys.modified = 100 + (Date.now()/1000);  // Future modification time.
    keys.encrypt(b);
    keys.upload(Weave.Service.cryptoKeysURL);
    
    do_check_false(Weave.Service.verifyAndFetchSymmetricKeys());
    do_check_eq(Status.login, LOGIN_FAILED_INVALID_PASSPHRASE);

  } finally {
    Weave.Svc.Prefs.resetBranch("");
    server.stop(do_test_finished);
  }
}