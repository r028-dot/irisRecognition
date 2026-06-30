-- יוצר את מסד הנתונים של המערכת.
-- אם הוא כבר קיים מהפעם הקודמת — מוחק ומתחיל מחדש.
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

-- טבלת משתמשים — כל נוסע שנרשם מקבל כאן שורה.
-- PassengerID הוא מספר תעודת הזהות של הנוסע.
CREATE TABLE Users (
    UserID          INT             IDENTITY(1,1)   PRIMARY KEY,
    PassengerID     NVARCHAR(20)    NOT NULL        UNIQUE,
    FullName        NVARCHAR(100)   NOT NULL,
    Nationality     NVARCHAR(50)    NOT NULL        DEFAULT '',
    CreatedAt       DATETIME2       NOT NULL        DEFAULT GETDATE(),
    IsActive        BIT             NOT NULL        DEFAULT 1
);
GO

-- טבלת תכונות קשתית העין.
-- לכל משתמש שורה לעין שמאל (Eye=0) ושורה לעין ימין (Eye=1).
-- כל שורה שומרת עד 3 תבניות רישום — ככה ממוצע HD מדויק יותר.
-- IrisCode = קוד קשתית (256 ביט) + מסכה (256 ביט) = 512 בייט לפני הצפנה.
-- אחרי הצפנה AES-256-CBC: IV (16 בייט) + טקסט מוצפן = 544 בייט.
-- מפתח ההצפנה נמצא אצל השרת בלבד, במשתנה סביבה IRIS_DB_AES_KEY.
CREATE TABLE IrisFeatures (
    FeatureID       INT             IDENTITY(1,1)   PRIMARY KEY,
    UserID          INT             NOT NULL,
    Eye             TINYINT         NOT NULL        CHECK (Eye IN (0, 1)),
    IrisCode1       VARBINARY(560)  NOT NULL,   -- תבנית ראשונה, חובה
    IrisCode2       VARBINARY(560)  NULL,        -- תבנית שנייה, אופציונלי
    IrisCode3       VARBINARY(560)  NULL,        -- תבנית שלישית, אופציונלי
    RegisteredAt    DATETIME2       NOT NULL        DEFAULT GETDATE(),

    CONSTRAINT FK_IrisFeatures_Users
        FOREIGN KEY (UserID) REFERENCES Users(UserID)
        ON DELETE CASCADE,

    -- מונע כפילות: רק רשומה אחת לכל משתמש לכל עין
    CONSTRAINT UQ_IrisFeatures_UserEye
        UNIQUE (UserID, Eye)
);
GO


-- טבלת שערים — כל שער הוא נקודת כניסה בשדה התעופה
CREATE TABLE Gates (
    GateID      INT             IDENTITY(1,1)   PRIMARY KEY,
    GateName    NVARCHAR(10)    NOT NULL        UNIQUE,   -- לדוגמה: A1, B3
    Terminal    NVARCHAR(10)    NOT NULL        DEFAULT '1'
);
GO


-- טבלת טיסות — כל טיסה קשורה לשער ויש לה חלון זמן לעלייה

CREATE TABLE Flights (
    FlightID        INT             IDENTITY(1,1)   PRIMARY KEY,
    FlightNumber    NVARCHAR(10)    NOT NULL,        -- לדוגמה: LY315
    Destination     NVARCHAR(100)   NOT NULL,
    DepartureTime   DATETIME2       NOT NULL,
    BoardingStart   DATETIME2       NOT NULL,        -- פתיחת השער לנוסעים
    BoardingEnd     DATETIME2       NOT NULL,        -- סגירת השער
    GateID          INT             NOT NULL,

    CONSTRAINT FK_Flights_Gates
        FOREIGN KEY (GateID) REFERENCES Gates(GateID),

    -- וידוא שחלון העלייה הגיוני
    CONSTRAINT CK_Flights_BoardingWindow
        CHECK (BoardingStart < BoardingEnd AND BoardingEnd <= DepartureTime)
);
GO

-- טבלת הזמנות — קישור בין נוסע לטיסה
-- HasBoarded מתעדכן ל-1 ברגע שהנוסע עובר את השער
CREATE TABLE Bookings (
    BookingID       INT             IDENTITY(1,1)   PRIMARY KEY,
    UserID          INT             NOT NULL,
    FlightID        INT             NOT NULL,
    SeatNumber      NVARCHAR(5)     NULL,
    BookedAt        DATETIME2       NOT NULL DEFAULT GETDATE(),
    HasBoarded      BIT             NOT NULL DEFAULT 0,

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


-- פרוצדורה: sp_EnrollUser
-- רושמת נוסע חדש עם קודי קשתית לשתי העיניים.
-- אם הנוסע כבר קיים — מעדכנת את הקודים.
CREATE OR ALTER PROCEDURE sp_EnrollUser
    @PassengerID    NVARCHAR(20),
    @FullName       NVARCHAR(100),
    @Nationality    NVARCHAR(50),
    @IrisLeft1      VARBINARY(560),       
    @IrisLeft2      VARBINARY(560) = NULL,  
    @IrisLeft3      VARBINARY(560) = NULL,  
    @IrisRight1     VARBINARY(560),         
    @IrisRight2     VARBINARY(560) = NULL,  
    @IrisRight3     VARBINARY(560) = NULL,  
    @NewUserID      INT OUTPUT
AS
BEGIN
    SET NOCOUNT ON;
    BEGIN TRANSACTION;
    BEGIN TRY
        -- אם הנוסע לא קיים עדיין — מוסיפים אותו
        IF NOT EXISTS (SELECT 1 FROM Users WHERE PassengerID = @PassengerID)
        BEGIN
            INSERT INTO Users (PassengerID, FullName, Nationality)
            VALUES (@PassengerID, @FullName, @Nationality);
        END

        SELECT @NewUserID = UserID FROM Users WHERE PassengerID = @PassengerID;

        -- עדכון או הוספה של תבניות עין שמאל
        MERGE IrisFeatures AS target
        USING (SELECT @NewUserID AS UserID, CAST(0 AS TINYINT) AS Eye) AS src
        ON target.UserID = src.UserID AND target.Eye = src.Eye
        WHEN MATCHED THEN
            UPDATE SET IrisCode1 = @IrisLeft1, IrisCode2 = @IrisLeft2,
                       IrisCode3 = @IrisLeft3, RegisteredAt = GETDATE()
        WHEN NOT MATCHED THEN
            INSERT (UserID, Eye, IrisCode1, IrisCode2, IrisCode3)
            VALUES (@NewUserID, 0, @IrisLeft1, @IrisLeft2, @IrisLeft3);

        -- עדכון או הוספה של תבניות עין ימין
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


-- פרוצדורה: sp_GetUserByID
-- מחזירה פרטי נוסע וקודי קשתית לפי PassengerID.
-- משמשת את השרת בעת אימות בשער.
CREATE OR ALTER PROCEDURE sp_GetUserByID
    @PassengerID NVARCHAR(20)
AS
BEGIN
    SET NOCOUNT ON;
    SELECT
        u.UserID,
        u.PassengerID,
        u.FullName,
        u.Nationality,
        f.Eye,
        f.IrisCode1,
        f.IrisCode2,
        f.IrisCode3
    FROM Users u
    JOIN IrisFeatures f ON f.UserID = u.UserID
    WHERE u.PassengerID = @PassengerID AND u.IsActive = 1;
END;
GO


-- פרוצדורה: sp_CheckGateAccess
-- בודקת אם נוסע מורשה לעבור בשער מסוים ברגע זה.
-- בודקת שהשער קיים, שיש הזמנה, ושאנחנו בתוך חלון הזמן של העלייה.
-- מחזירה AccessGranted=1 ומסמנת HasBoarded=1 אם הכל תקין.
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

    -- חיפוש השער לפי שם
    SELECT @GateID = GateID FROM Gates WHERE GateName = @GateName;
    IF @GateID IS NULL
    BEGIN
        SET @AccessGranted = 0;
        SET @Reason = 'שער לא קיים במערכת';
        RETURN;
    END

    -- חיפוש הזמנה תקפה: נוסע + שער נכון + בתוך חלון הזמן + טרם עלה
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

        -- הסבר מפורט מדוע הגישה נדחתה
        IF EXISTS (
            SELECT 1 FROM Bookings b
            JOIN Flights f ON f.FlightID = b.FlightID
            WHERE b.UserID = @UserID AND f.GateID = @GateID AND b.HasBoarded = 0
        )
            SET @Reason = 'Not within boarding time window';
        ELSE IF EXISTS (
            SELECT 1 FROM Bookings b
            JOIN Flights f ON f.FlightID = b.FlightID
            WHERE b.UserID = @UserID AND f.GateID = @GateID AND b.HasBoarded = 1
        )
            SET @Reason = 'Passenger already boarded';
        ELSE
            SET @Reason = 'No booking found for this gate';
        RETURN;
    END

    -- כל הבדיקות עברו — מסמנים שהנוסע עלה למטוס
    UPDATE Bookings SET HasBoarded = 1
    WHERE UserID = @UserID AND FlightID = @FlightID;

    SET @AccessGranted = 1;
    SET @Reason = 'Access granted';
END;
GO

-- אינדקסים לשיפור ביצועים בשאילתות נפוצות
CREATE INDEX IX_IrisFeatures_UserID    ON IrisFeatures(UserID);
CREATE INDEX IX_Flights_GateID         ON Flights(GateID);
CREATE INDEX IX_Flights_BoardingWindow ON Flights(BoardingStart, BoardingEnd);
CREATE INDEX IX_Bookings_UserID        ON Bookings(UserID);
CREATE INDEX IX_Bookings_FlightID      ON Bookings(FlightID);
GO


-- משתמש SQL מוגבל לשרת — הרשאת EXECUTE בלבד על הפרוצדורות.
-- השרת לא יכול לגשת ישירות לטבלאות, רק דרך הפרוצדורות.
IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE name = N'IrisServerLogin')
    CREATE LOGIN IrisServerLogin WITH PASSWORD = 'Ch@ngeMe2026!';
GO

IF NOT EXISTS (SELECT 1 FROM sys.database_principals WHERE name = N'IrisServerUser')
BEGIN
    CREATE USER IrisServerUser FOR LOGIN IrisServerLogin;
END
GO

GRANT EXECUTE ON sp_EnrollUser      TO IrisServerUser;
GRANT EXECUTE ON sp_GetUserByID     TO IrisServerUser;
GRANT EXECUTE ON sp_CheckGateAccess TO IrisServerUser;
GO

-- מניעת גישה ישירה לטבלאות
DENY SELECT, INSERT, UPDATE, DELETE ON IrisFeatures TO IrisServerUser;
DENY SELECT, INSERT, UPDATE, DELETE ON Users        TO IrisServerUser;
DENY SELECT, INSERT, UPDATE, DELETE ON Bookings     TO IrisServerUser;
GO
