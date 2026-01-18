package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"bytes"
	"encoding/json"
	"net/http"
	"path/filepath"
	"strconv"
	"time"

	"github.com/Ordspilleren/Tilted/server/web"
	"github.com/labstack/echo/v4"
	"github.com/labstack/echo/v4/middleware"
	"zombiezen.com/go/sqlite"
	"zombiezen.com/go/sqlite/sqlitex"
)

// SensorReading represents the data structure sent by the ESP32
type SensorReading struct {
	Reading     Reading `json:"reading"`
	GatewayID   string  `json:"gatewayId"`
	GatewayName string  `json:"gatewayName"`
}

// Reading contains the actual sensor data
type Reading struct {
	SensorID string  `json:"sensorId"`
	Gravity  float64 `json:"gravity"`
	Tilt     float64 `json:"tilt"`
	Temp     float64 `json:"temp"`
	Volt     float64 `json:"volt"`
	Interval int     `json:"interval"`
}

// DataPoint represents a point of data for frontend visualization
type DataPoint struct {
	Timestamp int64   `json:"timestamp"`
	Gravity   float64 `json:"gravity"`
	Tilt      float64 `json:"tilt"`
	Temp      float64 `json:"temp"`
	Volt      float64 `json:"volt"`
	Interval  int     `json:"interval"`
}

// SensorData represents the complete data for a sensor
type SensorData struct {
	SensorID    string      `json:"sensorId"`
	GatewayID   string      `json:"gatewayId"`
	GatewayName string      `json:"gatewayName"`
	DataPoints  []DataPoint `json:"dataPoints"`
}

// Global database connection pool
var dbPool *sqlitex.Pool
var brewfatherForwardURL string

func main() {
	// Initialize SQLite database
	var err error
	dbPool, err = initDB()
	if err != nil {
		log.Fatalf("Failed to initialize database: %v", err)
	}
	defer dbPool.Close()

	// Create a new Echo instance
	e := echo.New()

	// Add middleware
	e.Use(middleware.Logger())
	e.Use(middleware.Recover())
	e.Use(middleware.CORS())

	// Routes
	e.POST("/api/readings", handleSensorData)
	// Accept raw gateway JSON produced by the ESP32 gateway (the
	// JSON produced by EspNowReceiver::takePendingJson()). This makes it
	// trivial to point the gateway's Brewfather URL at this server so the
	// server can persist and optionally forward readings.
	e.POST("/api/publish", handleGatewayJson)
	e.GET("/api/sensors", getSensorIDs)
	e.GET("/api/readings/:sensorId", getSensorData)
	e.GET("/health", healthCheck)

	// Serve Svelte frontend for all other routes
	e.GET("/*", echo.WrapHandler(web.FrontEndHandler))

	// Start server
	port := ":8080"

	// Optional: forward incoming readings to a Brewfather-compatible endpoint.
	// Set environment variable BREWFATHER_FORWARD_URL to enable.
	brewfatherForwardURL = os.Getenv("BREWFATHER_FORWARD_URL")
	if brewfatherForwardURL != "" {
		log.Printf("Brewfather forward enabled -> %s", brewfatherForwardURL)
	}
	log.Printf("Starting server on port %s", port)
	if err := e.Start(port); err != http.ErrServerClosed {
		log.Fatal(err)
	}
}

// initDB initializes the SQLite database and creates necessary tables
func initDB() (*sqlitex.Pool, error) {
	databaseLocation := flag.String("database", "tilted.db", "")
	flag.Parse()

	// Ensure parent directory exists and the database file is present so
	// sqlite can open it. Some environments (containers, fresh deploys)
	// may not have created the file yet; proactively create it.
	dbPath := *databaseLocation
	dbDir := filepath.Dir(dbPath)
	if dbDir != "." && dbDir != "" {
		if err := os.MkdirAll(dbDir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create database directory: %v", err)
		}
	}

	if _, err := os.Stat(dbPath); os.IsNotExist(err) {
		f, err := os.OpenFile(dbPath, os.O_CREATE|os.O_EXCL, 0644)
		if err != nil {
			return nil, fmt.Errorf("failed to create database file: %v", err)
		}
		f.Close()
	}

	pool, err := sqlitex.NewPool(dbPath, sqlitex.PoolOptions{})
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %v", err)
	}

	// Get a connection to create tables
	conn, err := pool.Take(context.Background())
	if err != nil {
		return nil, fmt.Errorf("failed to get database connection: %v", err)
	}
	defer pool.Put(conn)

	// Create tables with a normalized schema
	createTablesSQL := `
    -- Sensors table to store sensor information
    CREATE TABLE IF NOT EXISTS sensors (
        id INTEGER PRIMARY KEY,
        sensor_id TEXT UNIQUE NOT NULL
    );
    
    -- Gateways table to store gateway information
    CREATE TABLE IF NOT EXISTS gateways (
        id INTEGER PRIMARY KEY,
        gateway_id TEXT NOT NULL,
        gateway_name TEXT NOT NULL,
        UNIQUE(gateway_id, gateway_name)
    );
    
    -- Readings table optimized for time-series data
    CREATE TABLE IF NOT EXISTS readings (
        timestamp INTEGER PRIMARY KEY, 
        sensor_id INTEGER NOT NULL,
        gateway_id INTEGER NOT NULL,
        gravity REAL NOT NULL,
        tilt REAL NOT NULL,
        temp REAL NOT NULL,
        volt REAL NOT NULL,
        interval INTEGER NOT NULL,
        FOREIGN KEY (sensor_id) REFERENCES sensors(id),
        FOREIGN KEY (gateway_id) REFERENCES gateways(id)
    ) WITHOUT ROWID;
    
    -- Create index for querying by sensor_id
    CREATE INDEX IF NOT EXISTS idx_readings_sensor_id ON readings(sensor_id);
    `

	err = sqlitex.ExecuteScript(conn, createTablesSQL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create tables: %v", err)
	}

	// Enable foreign keys
	err = sqlitex.ExecuteTransient(conn, "PRAGMA foreign_keys = ON", nil)
	if err != nil {
		return nil, fmt.Errorf("failed to enable foreign keys: %v", err)
	}

	return pool, nil
}

// handleSensorData processes the incoming sensor readings from ESP32
func handleSensorData(c echo.Context) error {
	sensorData := new(SensorReading)
	if err := c.Bind(sensorData); err != nil {
		return c.JSON(http.StatusBadRequest, map[string]string{
			"error": "Invalid request format",
		})
	}

	log.Printf("Received data from gateway: %s (%s)", sensorData.GatewayName, sensorData.GatewayID)
	log.Printf("Sensor: %s, Gravity: %.3f, Tilt: %.2f, Temp: %.2f",
		sensorData.Reading.SensorID,
		sensorData.Reading.Gravity,
		sensorData.Reading.Tilt,
		sensorData.Reading.Temp)

	// Save data to SQLite
	if err := saveToDatabase(sensorData); err != nil {
		log.Printf("Error saving to database: %v", err)
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"status": "error",
			"error":  "Failed to store metrics",
		})
	}

	// Optionally forward the reading to Brewfather (or any HTTP endpoint)
	if brewfatherForwardURL != "" {
		go func(sd *SensorReading) {
			if err := forwardToBrewfather(sd); err != nil {
				log.Printf("Failed to forward to Brewfather: %v", err)
			}
		}(sensorData)
	}

	return c.JSON(http.StatusOK, map[string]string{
		"status": "success",
	})
}

// saveToDatabase stores the sensor readings in SQLite with normalized schema
func saveToDatabase(data *SensorReading) error {
	// Get a connection from the pool
	conn, err := dbPool.Take(context.Background())
	if err != nil {
		return fmt.Errorf("failed to get database connection: %v", err)
	}
	defer dbPool.Put(conn)

	// Begin transaction using the Transaction helper
	endTx := sqlitex.Transaction(conn)
	defer func() {
		// endTx will automatically commit on nil error or rollback on error
		endTx(&err)
	}()

	// 1. Get or create sensor ID
	var sensorInternalID int64

	found := false
	err = sqlitex.Execute(conn,
		"SELECT id FROM sensors WHERE sensor_id = ?",
		&sqlitex.ExecOptions{
			Args: []any{data.Reading.SensorID},
			ResultFunc: func(stmt *sqlite.Stmt) error {
				sensorInternalID = stmt.ColumnInt64(0)
				found = true
				return nil
			},
		})

	if err != nil {
		return fmt.Errorf("failed to query sensor: %v", err)
	}

	if !found {
		// Insert new sensor
		err = sqlitex.Execute(conn,
			"INSERT INTO sensors (sensor_id) VALUES (?)",
			&sqlitex.ExecOptions{
				Args: []any{data.Reading.SensorID},
			})
		if err != nil {
			return fmt.Errorf("failed to insert sensor: %v", err)
		}

		// Get the last insert ID
		err = sqlitex.Execute(conn, "SELECT last_insert_rowid()", &sqlitex.ExecOptions{
			ResultFunc: func(stmt *sqlite.Stmt) error {
				sensorInternalID = stmt.ColumnInt64(0)
				return nil
			},
		})
		if err != nil {
			return fmt.Errorf("failed to get sensor ID: %v", err)
		}
	}

	// 2. Get or create gateway ID
	var gatewayInternalID int64

	found = false
	err = sqlitex.Execute(conn,
		"SELECT id FROM gateways WHERE gateway_id = ? AND gateway_name = ?",
		&sqlitex.ExecOptions{
			Args: []any{data.GatewayID, data.GatewayName},
			ResultFunc: func(stmt *sqlite.Stmt) error {
				gatewayInternalID = stmt.ColumnInt64(0)
				found = true
				return nil
			},
		})

	if err != nil {
		return fmt.Errorf("failed to query gateway: %v", err)
	}

	if !found {
		// Insert new gateway
		err = sqlitex.Execute(conn,
			"INSERT INTO gateways (gateway_id, gateway_name) VALUES (?, ?)",
			&sqlitex.ExecOptions{
				Args: []any{data.GatewayID, data.GatewayName},
			})
		if err != nil {
			return fmt.Errorf("failed to insert gateway: %v", err)
		}

		// Get the last insert ID
		err = sqlitex.Execute(conn, "SELECT last_insert_rowid()", &sqlitex.ExecOptions{
			ResultFunc: func(stmt *sqlite.Stmt) error {
				gatewayInternalID = stmt.ColumnInt64(0)
				return nil
			},
		})
		if err != nil {
			return fmt.Errorf("failed to get gateway ID: %v", err)
		}
	}

	// 3. Insert reading with the current timestamp
	timestamp := time.Now().UnixMilli()
	err = sqlitex.Execute(conn,
		`INSERT INTO readings (
			timestamp, sensor_id, gateway_id, gravity, tilt, temp, volt, interval
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
		&sqlitex.ExecOptions{
			Args: []any{
				timestamp, sensorInternalID, gatewayInternalID,
				data.Reading.Gravity, data.Reading.Tilt, data.Reading.Temp,
				data.Reading.Volt, data.Reading.Interval,
			},
		})
	if err != nil {
		return fmt.Errorf("failed to insert reading: %v", err)
	}

	log.Printf("Successfully saved metrics to SQLite database")
	return nil
}

// healthCheck provides a simple health check endpoint
func healthCheck(c echo.Context) error {
	// Get a connection from the pool to check if database is available
	conn, err := dbPool.Take(context.Background())
	if err != nil {
		return c.JSON(http.StatusServiceUnavailable, map[string]string{
			"status": "error",
			"error":  "Database connection failed",
			"time":   time.Now().Format(time.RFC3339),
		})
	}
	dbPool.Put(conn)

	return c.JSON(http.StatusOK, map[string]string{
		"status": "ok",
		"time":   time.Now().Format(time.RFC3339),
	})
}

// getSensorIDs retrieves all unique sensor IDs from the database
func getSensorIDs(c echo.Context) error {
	// Get a connection from the pool
	conn, err := dbPool.Take(context.Background())
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Failed to get database connection: %v", err),
		})
	}
	defer dbPool.Put(conn)

	var sensorIDs []string

	err = sqlitex.Execute(conn,
		"SELECT sensor_id FROM sensors ORDER BY sensor_id",
		&sqlitex.ExecOptions{
			ResultFunc: func(stmt *sqlite.Stmt) error {
				sensorIDs = append(sensorIDs, stmt.ColumnText(0))
				return nil
			},
		})

	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error querying database: %v", err),
		})
	}

	return c.JSON(http.StatusOK, sensorIDs)
}

// getSensorData retrieves data for a specific sensor from the database within a given time range
func getSensorData(c echo.Context) error {
	sensorID := c.Param("sensorId")
	if sensorID == "" {
		return c.JSON(http.StatusBadRequest, map[string]string{
			"error": "Sensor ID is required",
		})
	}

	// Get time range parameters
	startTimeStr := c.QueryParam("startTime")
	endTimeStr := c.QueryParam("endTime")

	var startTime, endTime int64
	var err error

	// Default end time to now if not provided or invalid
	if endTimeStr == "" {
		endTime = time.Now().UnixMilli()
	} else {
		endTime, err = strconv.ParseInt(endTimeStr, 10, 64)
		if err != nil {
			// Log invalid parameter but default gracefully
			log.Printf("Invalid endTime parameter: %s, defaulting to now. Error: %v", endTimeStr, err)
			endTime = time.Now().UnixMilli()
		}
	}

	// Default start time to 24 hours before end time if not provided or invalid
	if startTimeStr == "" {
		startTime = time.UnixMilli(endTime).Add(-24 * time.Hour).UnixMilli()
	} else {
		startTime, err = strconv.ParseInt(startTimeStr, 10, 64)
		if err != nil {
			// Log invalid parameter but default gracefully
			log.Printf("Invalid startTime parameter: %s, defaulting to 24h before (parsed/defaulted) endTime. Error: %v", startTimeStr, err)
			startTime = time.UnixMilli(endTime).Add(-24 * time.Hour).UnixMilli()
		}
	}

	// If parameters somehow result in an invalid range (e.g. user error or bad defaults from client)
	// default to a valid 24h range ending at the determined (or defaulted) endTime.
	if startTime > endTime {
		log.Printf("startTime (%d) was after endTime (%d), adjusting startTime to 24h before endTime", startTime, endTime)
		startTime = time.UnixMilli(endTime).Add(-24 * time.Hour).UnixMilli()
		// Alternative: return a 400 error
		// return c.JSON(http.StatusBadRequest, map[string]string{
		// 	"error": "startTime cannot be after endTime",
		// })
	}

	// Get a connection from the pool
	conn, err := dbPool.Take(context.Background())
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Failed to get database connection: %v", err),
		})
	}
	defer dbPool.Put(conn)

	// Prepare data structure for response
	sensorDataResult := SensorData{
		SensorID:   sensorID,
		DataPoints: []DataPoint{},
	}

	// Query the database for sensor data with JOINs to get the necessary information
	query := `
    SELECT 
        r.timestamp, s.sensor_id, g.gateway_id, g.gateway_name,
        r.gravity, r.tilt, r.temp, r.volt, r.interval
    FROM 
        readings r
    JOIN 
        sensors s ON r.sensor_id = s.id
    JOIN 
        gateways g ON r.gateway_id = g.id
    WHERE 
        s.sensor_id = ? AND r.timestamp >= ? AND r.timestamp <= ?
    ORDER BY 
        r.timestamp ASC
    `

	err = sqlitex.Execute(conn, query, &sqlitex.ExecOptions{
		Args: []any{sensorID, startTime, endTime},
		ResultFunc: func(stmt *sqlite.Stmt) error {
			// Set gateway info if not already set (it should be the same for all readings of a sensor in a request)
			if sensorDataResult.GatewayID == "" {
				sensorDataResult.GatewayID = stmt.ColumnText(2)   // gateway_id
				sensorDataResult.GatewayName = stmt.ColumnText(3) // gateway_name
			}

			// Add data point
			dataPoint := DataPoint{
				Timestamp: stmt.ColumnInt64(0),      // timestamp
				Gravity:   stmt.ColumnFloat(4),      // gravity
				Tilt:      stmt.ColumnFloat(5),      // tilt
				Temp:      stmt.ColumnFloat(6),      // temp
				Volt:      stmt.ColumnFloat(7),      // volt
				Interval:  int(stmt.ColumnInt64(8)), // interval
			}

			sensorDataResult.DataPoints = append(sensorDataResult.DataPoints, dataPoint)
			return nil
		},
	})

	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error querying data: %v", err),
		})
	}

	// If no data points were found, GatewayID and GatewayName might be empty.
	// If sensorDataResult.DataPoints is empty and you still want to show gateway/sensor info,
	// you might need a separate query for sensor metadata if it's critical.
	// For now, if no data points, these might remain empty in the response.

	return c.JSON(http.StatusOK, sensorDataResult)
}

// handleGatewayJson accepts the lightweight JSON produced by the ESP32
// gateway (EspNowReceiver::takePendingJson) and maps it into the server's
// SensorReading type so it can be stored/forwarded in the same pipeline.
func handleGatewayJson(c echo.Context) error {
	var payload map[string]any
	if err := c.Bind(&payload); err != nil {
		return c.JSON(http.StatusBadRequest, map[string]string{"error": "invalid gateway payload"})
	}

	// Map fields with best-effort conversions.
	reading := Reading{}
	if v, ok := payload["gravity"]; ok {
		if f, ok2 := v.(float64); ok2 {
			reading.Gravity = f
		}
	}
	if v, ok := payload["angle"]; ok {
		if f, ok2 := v.(float64); ok2 {
			reading.Tilt = f
		}
	}
	if v, ok := payload["temp"]; ok {
		if f, ok2 := v.(float64); ok2 {
			reading.Temp = f
		}
	}
	if v, ok := payload["battery"]; ok {
		if f, ok2 := v.(float64); ok2 {
			// Gateway sends battery as volts (float)
			reading.Volt = f
		}
	}
	if v, ok := payload["interval"]; ok {
		if fi, ok2 := v.(float64); ok2 {
			reading.Interval = int(fi)
		}
	}

	sensorId := "unknown"
	if v, ok := payload["name"]; ok {
		if s, ok2 := v.(string); ok2 && s != "" {
			sensorId = s
		}
	}

	// Use remote address as a fallback gateway identifier
	gatewayID := c.Request().RemoteAddr
	gatewayName := gatewayID

	sr := &SensorReading{
		Reading:     reading,
		GatewayID:   gatewayID,
		GatewayName: gatewayName,
	}
	// Use sensorID as the reading.SensorID
	sr.Reading.SensorID = sensorId

	// Save and optionally forward using existing logic
	if err := saveToDatabase(sr); err != nil {
		log.Printf("Error saving gateway payload: %v", err)
		return c.JSON(http.StatusInternalServerError, map[string]string{"status": "error", "error": "Failed to store metrics"})
	}

	if brewfatherForwardURL != "" {
		go func(sd *SensorReading) {
			if err := forwardToBrewfather(sd); err != nil {
				log.Printf("Failed to forward to Brewfather: %v", err)
			}
		}(sr)
	}

	return c.JSON(http.StatusOK, map[string]string{"status": "ok"})
}

// forwardToBrewfather sends the incoming sensor reading JSON to the configured
// Brewfather-forward URL. It's intentionally simple: one attempt, logged on
// failure. Caller should run this in a goroutine if they don't want blocking.
func forwardToBrewfather(data *SensorReading) error {
	// Reuse the incoming JSON shape for forwarding unless the target
	// requires a different payload. This keeps the gateway side simple
	// — you can point the gateway's Brewfather URL at this server and it
	// will be proxied onward as configured here.
	b, err := json.Marshal(data)
	if err != nil {
		return fmt.Errorf("marshal: %w", err)
	}

	req, err := http.NewRequest("POST", brewfatherForwardURL, bytes.NewReader(b))
	if err != nil {
		return fmt.Errorf("create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("http post: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("unexpected status: %s", resp.Status)
	}
	return nil
}
