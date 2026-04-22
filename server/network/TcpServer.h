#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>
#include "../iris/IrisProcessor.h"

// Listens on a TCP port and dispatches each accepted connection to ConnectionHandler
class TcpServer {
public:
    TcpServer(std::shared_ptr<IrisProcessor> processor, int port);
    ~TcpServer();

    void run();  // Blocking – accepts connections until stopped

private:
    std::shared_ptr<IrisProcessor> m_processor;
    int    m_port;
    SOCKET m_listenSocket = INVALID_SOCKET;
};
