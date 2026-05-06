#pragma once
#include <memory>
#include <vector>
#include <string>
#include "../database/DatabaseManager.h"
#include "../models/AuthResult.h"
#include "FeatureExtractor.h"
#include "IrisMatcher.h"

// Orchestrates: raw image → feature extraction → DB lookup → matching → AuthResult
class IrisProcessor {
public:
    explicit IrisProcessor(std::shared_ptr<DatabaseManager> db);

    // Verify identity: extract IrisCode from image, compare with the stored code for
    // passportNumber. eye: 0=Left, 1=Right.
    AuthResult verify(const std::string& passportNumber,
                      const std::vector<uint8_t>& imageData,
                      int eye);

    // Enroll: extract features from eye images and persist to DB.
    // imagesLeft / imagesRight: 1–3 images each (first is mandatory).
    // All templates are stored in one DB row per eye.
    AuthResult enroll(const std::string& passportNumber,
                      const std::string& fullName,
                      const std::string& nationality,
                      const std::vector<std::vector<uint8_t>>& imagesLeft,
                      const std::vector<std::vector<uint8_t>>& imagesRight);

private:
    std::shared_ptr<DatabaseManager> m_db;
    FeatureExtractor m_extractor;
    IrisMatcher      m_matcher;

    static constexpr double MATCH_THRESHOLD = 0.32;   // Hamming below this = MATCH
    static constexpr int    MIN_VALID_BITS  = 1000;   // minimum unmasked bits
};
