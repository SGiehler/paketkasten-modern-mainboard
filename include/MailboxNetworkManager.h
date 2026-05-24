#ifndef MAILBOX_NETWORK_MANAGER_H
#define MAILBOX_NETWORK_MANAGER_H

#include <ESPAsyncWebServer.h>

class MailboxNetworkManager {
public:
    MailboxNetworkManager();
    void begin();

private:
    void setupWebServer();
    void connectWiFi();

    AsyncWebServer _server;
};

extern MailboxNetworkManager mailboxNetworkManager;

#endif
