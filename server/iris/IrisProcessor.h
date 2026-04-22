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

    // Enroll: extract features from both eye images and persist to DB.
    AuthResult enroll(const std::string& passportNumber,
                      const std::string& fullName,
                      const std::string& nationality,
                      const std::vector<uint8_t>& imageDataLeft,
                      const std::vector<uint8_t>& imageDataRight);

private:
    std::shared_ptr<DatabaseManager> m_db;
    FeatureExtractor m_extractor;
    IrisMatcher      m_matcher;

    static constexpr double MATCH_THRESHOLD = 0.32;   // Hamming below this = MATCH
    static constexpr int    MIN_VALID_BITS  = 1000;   // minimum unmasked bits
};
