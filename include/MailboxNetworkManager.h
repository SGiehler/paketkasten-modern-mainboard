#ifndef MAILBOX_NETWORK_MANAGER_H
#define MAILBOX_NETWORK_MANAGER_H

#include <ESPAsyncWebServer.h>

class MailboxNetworkManager {
public:
    MailboxNetworkManager();
    void begin();
    void update();

private:
    void setupWebServer();
    void connectWiFi();

    AsyncWebServer _server;
    String _configuredSsid;
    String _configuredPassword;
    bool _softApMode;
    unsigned long _lastWifiRetryMs;
};

extern MailboxNetworkManager mailboxNetworkManager;

#endif
