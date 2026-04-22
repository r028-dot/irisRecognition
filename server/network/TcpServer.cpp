// ============================================================
//  TcpServer.cpp
//  Listens on a TCP port; dispatches each accepted socket to a
//  ConnectionHandler running in the shared ThreadPool.
// ============================================================
#include "TcpServer.h"
#include "ConnectionHandler.h"
#include "../security/Encryptor.h"
#include "../utils/Logger.h"
#include "../utils/ThreadPool.h"
#include <stdexcept>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

// ─────────────────────────────────────────────────────────────────────────────
// Constructor – initialise Winsock and create the listening socket
// ─────────────────────────────────────────────────────────────────────────────
TcpServer::TcpServer(std::shared_ptr<IrisProcessor> processor, int port)
    : m_processor(std::move(processor))
    , m_port(port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        throw std::runtime_error("TcpServer: WSAStartup failed: "
                                 + std::to_string(WSAGetLastError()));

    m_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("TcpServer: socket() failed: "
                                 + std::to_string(WSAGetLastError()));
    }

    // Allow rapid restart without TIME_WAIT delay
    int optVal = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&optVal), sizeof(optVal));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<u_short>(m_port));

    if (::bind(m_listenSocket,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        WSACleanup();
        throw std::runtime_error("TcpServer: bind() failed on port "
                                 + std::to_string(m_port) + ": "
                                 + std::to_string(WSAGetLastError()));
    }

    if (::listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        WSACleanup();
        throw std::runtime_error("TcpServer: listen() failed: "
                                 + std::to_string(WSAGetLastError()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────
TcpServer::~TcpServer()
{
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    WSACleanup();
}

// ─────────────────────────────────────────────────────────────────────────────
// run  – blocking accept loop
// ─────────────────────────────────────────────────────────────────────────────
void TcpServer::run()
{
    // Load the AES key and create the encryptor once
    Encryptor encryptor;

    // Thread pool: 8 workers (matches default numWorkers in config)
    ThreadPool pool(8);

    Logger::instance().info("TcpServer: listening on port "
                            + std::to_string(m_port));

    for (;;) {
        sockaddr_in clientAddr{};
        int         addrLen   = static_cast<int>(sizeof(clientAddr));
        SOCKET      clientSock = ::accept(
            m_listenSocket,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &addrLen);

        if (clientSock == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAENOTSOCK)
                break;  // Server is shutting down
            Logger::instance().warning("TcpServer: accept() error: "
                                       + std::to_string(err));
            continue;
        }

        // Log client IP
        char ipStr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        Logger::instance().info(std::string("TcpServer: connection from ") + ipStr);

        // Move ownership of the socket into the lambda captured by the pool
        pool.enqueue([sock = clientSock,
                      proc = m_processor,
                      &enc  = encryptor]() mutable
        {
            ConnectionHandler handler(sock, proc, enc);
            handler.handle();
        });
    }
}
