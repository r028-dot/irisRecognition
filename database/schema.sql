-- ============================================================
--  Iris Recognition System - SQL Server Database Schema
-- ============================================================

-- Drop and recreate cleanly
IF EXISTS (SELECT name FROM sys.databases WHERE name = N'IrisRecognitionDB')
BEGIN
    ALTER DATABASE IrisRecognitionDB SET SINGLE_USER WITH ROLLBACK IMMEDIATE;
    DROP DATABASE IrisRecognitionDB;
END
GO

CREATE DATABASE IrisRecognitionDB;
GO

USE IrisRecognitionDB;
GO

-- ------------------------------------------------------------
-- Table: Users
-- PassportNumber is the unique business key used for lookup
-- ------------------------------------------------------------
CREATE TABLE Users (
    UserID          INT             IDENTITY(1,1)   PRIMARY KEY,
    PassportNumber  NVARCHAR(20)    NOT NULL        UNIQUE,
    FullName        NVARCHAR(100)   NOT NULL,
    Nationality     NVARCHAR(50)    NOT NULL        DEFAULT '',
    CreatedAt       DATETIME2       NOT NULL        DEFAULT GETDATE(),
    IsActive        BIT             NOT NULL        DEFAULT 1
);
GO

-- ------------------------------------------------------------
-- Table: IrisFeatures
-- One row per user per eye.  Up to 3 enrollment templates are
-- stored in three columns so that the DB enforces exactly one
-- record per (UserID, Eye) pair.
-- Each IrisCodeN = bits[256] || mask[256] = 512 bytes.
-- IrisCode1 is mandatory; IrisCode2 / IrisCode3 are optional.
-- Eye: 0 = Left, 1 = Right
-- ------------------------------------------------------------
CREATE TABLE IrisFeatures (
    FeatureID       INT             IDENTITY(1,1)   PRIMARY KEY,
    UserID          INT             NOT NULL,
    Eye             TINYINT         NOT NULL        CHECK (Eye IN (0, 1)),
    IrisCode1       VARBINARY(512)  NOT NULL,       -- primary template
    IrisCode2       VARBINARY(512)  NULL,           -- 2nd enrollment image (optional)
    IrisCode3       VARBINARY(512)  NULL,           -- 3rd enrollment image (optional)
    RegisteredAt    DATETIME2       NOT NULL        DEFAULT GETDATE(),

    CONSTRAINT FK_IrisFeatures_Users
        FOREIGN KEY (UserID) REFERENCES Users(UserID)
        ON DELETE CASCADE,

    CONSTRAINT UQ_IrisFeatures_UserEye
        UNIQUE (UserID, Eye)
);
GO

-- ------------------------------------------------------------
-- Table: RecognitionLog
-- Audit log for every recognition attempt
-- ------------------------------------------------------------
CREATE TABLE RecognitionLog (
    LogID           INT             IDENTITY(1,1)   PRIMARY KEY,
    AttemptedAt     DATETIME2       NOT NULL        DEFAULT GETDATE(),
    MatchedUserID   INT             NULL,
    Eye             TINYINT         NOT NULL        CHECK (Eye IN (0, 1)),
    Success         BIT             NOT NULL,
    HammingDistance FLOAT           NULL,
    Notes           NVARCHAR(255)   NULL,

    CONSTRAINT FK_RecognitionLog_Users
        FOREIGN KEY (MatchedUserID) REFERENCES Users(UserID)
        ON DELETE SET NULL
);
GO

-- ============================================================
--  Stored Procedures
-- ============================================================

-- Enroll a new user with both iris codes (up to 3 templates per eye)
CREATE OR ALTER PROCEDURE sp_EnrollUser
    @PassportNumber NVARCHAR(20),
    @FullName       NVARCHAR(100),
    @Nationality    NVARCHAR(50),
    @IrisLeft1      VARBINARY(512),         -- mandatory
    @IrisLeft2      VARBINARY(512) = NULL,  -- optional 2nd left template
    @IrisLeft3      VARBINARY(512) = NULL,  -- optional 3rd left template
    @IrisRight1     VARBINARY(512),         -- mandatory
    @IrisRight2     VARBINARY(512) = NULL,  -- optional 2nd right template
    @IrisRight3     VARBINARY(512) = NULL,  -- optional 3rd right template
    @NewUserID      INT OUTPUT
AS
BEGIN
    SET NOCOUNT ON;
    BEGIN TRANSACTION;
    BEGIN TRY
        IF NOT EXISTS (SELECT 1 FROM Users WHERE PassportNumber = @PassportNumber)
        BEGIN
            INSERT INTO Users (PassportNumber, FullName, Nationality)
            VALUES (@PassportNumber, @FullName, @Nationality);
        END

        SELECT @NewUserID = UserID FROM Users WHERE PassportNumber = @PassportNumber;

        -- Upsert left eye — all 3 templates in one row
        MERGE IrisFeatures AS target
        USING (SELECT @NewUserID AS UserID, CAST(0 AS TINYINT) AS Eye) AS src
        ON target.UserID = src.UserID AND target.Eye = src.Eye
        WHEN MATCHED THEN
            UPDATE SET IrisCode1 = @IrisLeft1, IrisCode2 = @IrisLeft2,
                       IrisCode3 = @IrisLeft3, RegisteredAt = GETDATE()
        WHEN NOT MATCHED THEN
            INSERT (UserID, Eye, IrisCode1, IrisCode2, IrisCode3)
            VALUES (@NewUserID, 0, @IrisLeft1, @IrisLeft2, @IrisLeft3);

        -- Upsert right eye — all 3 templates in one row
        MERGE IrisFeatures AS target
        USING (SELECT @NewUserID AS UserID, CAST(1 AS TINYINT) AS Eye) AS src
        ON target.UserID = src.UserID AND target.Eye = src.Eye
        WHEN MATCHED THEN
            UPDATE SET IrisCode1 = @IrisRight1, IrisCode2 = @IrisRight2,
                       IrisCode3 = @IrisRight3, RegisteredAt = GETDATE()
        WHEN NOT MATCHED THEN
            INSERT (UserID, Eye, IrisCode1, IrisCode2, IrisCode3)
            VALUES (@NewUserID, 1, @IrisRight1, @IrisRight2, @IrisRight3);

        COMMIT TRANSACTION;
    END TRY
    BEGIN CATCH
        ROLLBACK TRANSACTION;
        THROW;
    END CATCH
END;
GO

-- Load a user's data + iris codes by passport number
CREATE OR ALTER PROCEDURE sp_GetUserByPassport
    @PassportNumber NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;
    SELECT
        u.UserID,
        u.PassportNumber,
        u.FullName,
        u.Nationality,
        f.Eye,
        f.IrisCode1,
        f.IrisCode2,
        f.IrisCode3
    FROM Users u
    JOIN IrisFeatures f ON f.UserID = u.UserID
    WHERE u.PassportNumber = @PassportNumber AND u.IsActive = 1;
END;
GO

-- Log one recognition attempt
CREATE OR ALTER PROCEDURE sp_LogAuthAttempt
    @MatchedUserID  INT,
    @Eye            TINYINT,
    @Success        BIT,
    @HammingDist    FLOAT,
    @Notes          NVARCHAR(255) = NULL
AS
BEGIN
    SET NOCOUNT ON;
    INSERT INTO RecognitionLog (MatchedUserID, Eye, Success, HammingDistance, Notes)
    VALUES (@MatchedUserID, @Eye, @Success, @HammingDist, @Notes);
END;
GO

-- ============================================================
--  Indexes for performance
-- ============================================================
CREATE INDEX IX_IrisFeatures_UserID        ON IrisFeatures(UserID);
CREATE INDEX IX_RecognitionLog_AttemptedAt ON RecognitionLog(AttemptedAt DESC);
CREATE INDEX IX_RecognitionLog_UserID      ON RecognitionLog(MatchedUserID);
GO

-- ============================================================
--  Table: Gates — שערי עלייה למטוס
-- ============================================================
CREATE TABLE Gates (
    GateID      INT             IDENTITY(1,1)   PRIMARY KEY,
    GateName    NVARCHAR(10)    NOT NULL        UNIQUE,   -- A1, B3, C12...
    Terminal    NVARCHAR(10)    NOT NULL        DEFAULT '1'
);
GO

-- ============================================================
--  Table: Flights — טיסות
-- ============================================================
CREATE TABLE Flights (
    FlightID        INT             IDENTITY(1,1)   PRIMARY KEY,
    FlightNumber    NVARCHAR(10)    NOT NULL,             -- LY315, EK202...
    Destination     NVARCHAR(100)   NOT NULL,             -- London, Paris...
    DepartureTime   DATETIME2       NOT NULL,             -- מתי הטיסה יוצאת
    BoardingStart   DATETIME2       NOT NULL,             -- מתי פותחים את השער
    BoardingEnd     DATETIME2       NOT NULL,             -- מתי סוגרים את השער
    GateID          INT             NOT NULL,

    CONSTRAINT FK_Flights_Gates
        FOREIGN KEY (GateID) REFERENCES Gates(GateID),

    CONSTRAINT CK_Flights_BoardingWindow
        CHECK (BoardingStart < BoardingEnd AND BoardingEnd <= DepartureTime)
);
GO

-- ============================================================
--  Table: Bookings — הזמנות (מי עולה על איזה טיסה)
-- ============================================================
CREATE TABLE Bookings (
    BookingID       INT             IDENTITY(1,1)   PRIMARY KEY,
    UserID          INT             NOT NULL,
    FlightID        INT             NOT NULL,
    SeatNumber      NVARCHAR(5)     NULL,                 -- 14A, 32B...
    BookedAt        DATETIME2       NOT NULL DEFAULT GETDATE(),
    HasBoarded      BIT             NOT NULL DEFAULT 0,   -- האם כבר עלה למטוס

    CONSTRAINT FK_Bookings_Users
        FOREIGN KEY (UserID) REFERENCES Users(UserID)
        ON DELETE CASCADE,

    CONSTRAINT FK_Bookings_Flights
        FOREIGN KEY (FlightID) REFERENCES Flights(FlightID)
        ON DELETE CASCADE,

    CONSTRAINT UQ_Bookings_UserFlight
        UNIQUE (UserID, FlightID)
);
GO

-- ============================================================
--  Stored Procedure: sp_CheckGateAccess
--  בודקת האם נוסע מורשה לעבור בשער מסוים ברגע זה
--  מחזירה: AccessGranted (1/0), FlightNumber, SeatNumber, Reason
-- ============================================================
CREATE OR ALTER PROCEDURE sp_CheckGateAccess
    @UserID         INT,
    @GateName       NVARCHAR(10),
    @AccessGranted  BIT         OUTPUT,
    @FlightNumber   NVARCHAR(10) OUTPUT,
    @SeatNumber     NVARCHAR(5)  OUTPUT,
    @Reason         NVARCHAR(255) OUTPUT
AS
BEGIN
    SET NOCOUNT ON;

    DECLARE @Now DATETIME2 = GETDATE();
    DECLARE @GateID INT;
    DECLARE @FlightID INT;

    -- מצא את ה-GateID לפי שם השער
    SELECT @GateID = GateID FROM Gates WHERE GateName = @GateName;
    IF @GateID IS NULL
    BEGIN
        SET @AccessGranted = 0;
        SET @Reason = 'שער לא קיים במערכת';
        RETURN;
    END

    -- חפש הזמנה תקפה: הנוסע רשום לטיסה שיוצאת מהשער הזה, ועכשיו בחלון העלייה
    SELECT TOP 1
        @FlightID    = f.FlightID,
        @FlightNumber = f.FlightNumber,
        @SeatNumber  = b.SeatNumber
    FROM Bookings b
    JOIN Flights f ON f.FlightID = b.FlightID
    WHERE
        b.UserID    = @UserID
        AND f.GateID = @GateID
        AND @Now    BETWEEN f.BoardingStart AND f.BoardingEnd
        AND b.HasBoarded = 0;

    IF @FlightID IS NULL
    BEGIN
        SET @AccessGranted = 0;
        SET @FlightNumber  = NULL;
        SET @SeatNumber    = NULL;

        -- סיבה מפורטת: האם יש הזמנה אבל בזמן הלא נכון?
        IF EXISTS (
            SELECT 1 FROM Bookings b
            JOIN Flights f ON f.FlightID = b.FlightID
            WHERE b.UserID = @UserID AND f.GateID = @GateID AND b.HasBoarded = 0
        )
            SET @Reason = 'לא בזמן העלייה למטוס';
        ELSE IF EXISTS (
            SELECT 1 FROM Bookings b
            JOIN Flights f ON f.FlightID = b.FlightID
            WHERE b.UserID = @UserID AND f.GateID = @GateID AND b.HasBoarded = 1
        )
            SET @Reason = 'הנוסע כבר עלה למטוס';
        ELSE
            SET @Reason = 'אין הזמנה לטיסה בשער זה';
        RETURN;
    END

    -- הכל תקין — עדכן HasBoarded=1 וסמן שעבר
    UPDATE Bookings SET HasBoarded = 1
    WHERE UserID = @UserID AND FlightID = @FlightID;

    SET @AccessGranted = 1;
    SET @Reason = 'גישה מאושרת';
END;
GO

-- ============================================================
--  Indexes for the new tables
-- ============================================================
CREATE INDEX IX_Flights_GateID         ON Flights(GateID);
CREATE INDEX IX_Flights_BoardingWindow ON Flights(BoardingStart, BoardingEnd);
CREATE INDEX IX_Bookings_UserID        ON Bookings(UserID);
CREATE INDEX IX_Bookings_FlightID      ON Bookings(FlightID);
GO
