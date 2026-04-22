// Entry point - loads config, initialises logger, DatabaseManager, IrisProcessor, TcpServer
#include <iostream>
#include <memory>
#include "config/ServerConfig.h"
#include "database/DatabaseManager.h"
#include "iris/IrisProcessor.h"
#include "network/TcpServer.h"
#include "utils/Logger.h"

int main()
{
    Logger::instance().init("iris_server.log");
    Logger::instance().info("=== Iris Recognition Server starting ===");

    try {
        ServerConfig cfg = ServerConfig::loadFromFile("config/config.json");
        Logger::instance().info("Config loaded - port=" + std::to_string(cfg.port)
                                + " workers=" + std::to_string(cfg.numWorkers));

        auto db = std::make_shared<DatabaseManager>(cfg.dbConnectionString);
        Logger::instance().info("Connected to IrisRecognitionDB");

        auto processor = std::make_shared<IrisProcessor>(db);

        Logger::instance().info("Listening on port " + std::to_string(cfg.port) + "...");
        TcpServer server(processor, cfg.port);
        server.run();

    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Fatal: ") + e.what());
        return 1;
    }

    Logger::instance().info("Server stopped.");
    return 0;
}
