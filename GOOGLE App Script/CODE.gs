// ====================================================================
// GOOGLE APPS SCRIPT - DATA LOGGING WERENG SYSTEM
// ====================================================================
// 
// CARA SETUP:
// 1. Buka Google Spreadsheet baru
// 2. Extensions > Apps Script
// 3. Paste kode ini, ganti dengan kode lama
// 4. Deploy > New Deployment > Web App
//    - Execute as: Me
//    - Who has access: Anyone
// 5. Copy URL Web App yang diberikan
// 6. Paste URL tersebut ke ESP32 code
//
// ====================================================================

// Konfigurasi
const SHEET_NAME = "Wereng Data Log"; // Nama sheet
const MAX_ROWS = 10000; // Maksimal baris data (auto-delete old data)

function doGet(e) {
  return ContentService.createTextOutput(JSON.stringify({
    'status': 'error',
    'message': 'Use POST method'
  })).setMimeType(ContentService.MimeType.JSON);
}

function doPost(e) {
  try {
    // Parse data dari ESP32
    var data = JSON.parse(e.postData.contents);
    
    // Validasi data
    if (!data.timestamp || data.temperature === undefined || 
        data.humidity === undefined || data.wereng === undefined || 
        data.relay === undefined) {
      return ContentService.createTextOutput(JSON.stringify({
        'status': 'error',
        'message': 'Missing required fields'
      })).setMimeType(ContentService.MimeType.JSON);
    }
    
    // Buka spreadsheet
    var sheet = getOrCreateSheet(SHEET_NAME);
    
    // Jika sheet kosong, buat header
    if (sheet.getLastRow() === 0) {
      sheet.appendRow([
        'Timestamp',
        'Date',
        'Time', 
        'Temperature (°C)',
        'Humidity (%)',
        'Wereng Count',
        'Relay Status',
        'WiFi RSSI (dBm)',
        'Notes'
      ]);
      
      // Format header
      var headerRange = sheet.getRange(1, 1, 1, 9);
      headerRange.setFontWeight('bold');
      headerRange.setBackground('#4285F4');
      headerRange.setFontColor('#FFFFFF');
      headerRange.setHorizontalAlignment('center');
    }
    
    // Parse timestamp
    var timestamp = new Date(data.timestamp);
    var dateStr = Utilities.formatDate(timestamp, "GMT+7", "yyyy-MM-dd");
    var timeStr = Utilities.formatDate(timestamp, "GMT+7", "HH:mm:ss");
    
    // Relay status sebagai text
    var relayStatus = data.relay === 1 ? "ON" : "OFF";
    
    // WiFi RSSI (optional)
    var rssi = data.rssi || "N/A";
    
    // Append data baru
    sheet.appendRow([
      timestamp,
      dateStr,
      timeStr,
      data.temperature,
      data.humidity,
      data.wereng,
      relayStatus,
      rssi,
      data.notes || ""
    ]);
    
    // Auto-delete old rows jika melebihi MAX_ROWS
    if (sheet.getLastRow() > MAX_ROWS + 1) {
      sheet.deleteRows(2, sheet.getLastRow() - MAX_ROWS);
    }
    
    // Format kolom
    autoFormatSheet(sheet);
    
    // Response sukses
    return ContentService.createTextOutput(JSON.stringify({
      'status': 'success',
      'message': 'Data logged successfully',
      'row': sheet.getLastRow()
    })).setMimeType(ContentService.MimeType.JSON);
    
  } catch (error) {
    // Response error
    return ContentService.createTextOutput(JSON.stringify({
      'status': 'error',
      'message': error.toString()
    })).setMimeType(ContentService.MimeType.JSON);
  }
}

// Helper: Get or Create Sheet
function getOrCreateSheet(sheetName) {
  var spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = spreadsheet.getSheetByName(sheetName);
  
  if (!sheet) {
    sheet = spreadsheet.insertSheet(sheetName);
  }
  
  return sheet;
}

// Helper: Auto Format Sheet
function autoFormatSheet(sheet) {
  var lastRow = sheet.getLastRow();
  
  if (lastRow <= 1) return; // Skip jika hanya header
  
  // Format kolom Timestamp
  var timestampRange = sheet.getRange(2, 1, lastRow - 1, 1);
  timestampRange.setNumberFormat("yyyy-MM-dd HH:mm:ss");
  
  // Format kolom Temperature
  var tempRange = sheet.getRange(2, 4, lastRow - 1, 1);
  tempRange.setNumberFormat("0.0");
  
  // Format kolom Humidity
  var humRange = sheet.getRange(2, 5, lastRow - 1, 1);
  humRange.setNumberFormat("0.0");
  
  // Auto-resize columns
  sheet.autoResizeColumns(1, 9);
  
  // Freeze header row
  sheet.setFrozenRows(1);
  
  // Conditional formatting untuk relay status
  var relayRange = sheet.getRange(2, 7, lastRow - 1, 1);
  
  // Clear existing rules
  var rules = sheet.getConditionalFormatRules();
  var newRules = rules.filter(function(rule) {
    return rule.getRanges()[0].getColumn() !== 7;
  });
  
  // Add new rules
  var onRule = SpreadsheetApp.newConditionalFormatRule()
    .whenTextEqualTo("ON")
    .setBackground("#34A853")
    .setFontColor("#FFFFFF")
    .setRanges([relayRange])
    .build();
    
  var offRule = SpreadsheetApp.newConditionalFormatRule()
    .whenTextEqualTo("OFF")
    .setBackground("#EA4335")
    .setFontColor("#FFFFFF")
    .setRanges([relayRange])
    .build();
  
  newRules.push(onRule);
  newRules.push(offRule);
  sheet.setConditionalFormatRules(newRules);
}

// Test function (untuk testing manual)
function testDoPost() {
  var testData = {
    postData: {
      contents: JSON.stringify({
        timestamp: new Date().toISOString(),
        temperature: 28.5,
        humidity: 65.3,
        wereng: 15,
        relay: 1,
        rssi: -65,
        notes: "Test data"
      })
    }
  };
  
  var response = doPost(testData);
  Logger.log(response.getContent());
}

// Function untuk membuat chart otomatis
function createCharts() {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(SHEET_NAME);
  var lastRow = sheet.getLastRow();
  
  if (lastRow <= 1) {
    Logger.log("No data available for charts");
    return;
  }
  
  // Chart 1: Temperature & Humidity over Time
  var chart1 = sheet.newChart()
    .setChartType(Charts.ChartType.LINE)
    .addRange(sheet.getRange(1, 3, lastRow, 1)) // Time
    .addRange(sheet.getRange(1, 4, lastRow, 1)) // Temperature
    .addRange(sheet.getRange(1, 5, lastRow, 1)) // Humidity
    .setPosition(2, 10, 0, 0)
    .setOption('title', 'Temperature & Humidity Trend')
    .setOption('width', 600)
    .setOption('height', 400)
    .build();
  
  sheet.insertChart(chart1);
  
  // Chart 2: Wereng Count over Time
  var chart2 = sheet.newChart()
    .setChartType(Charts.ChartType.COLUMN)
    .addRange(sheet.getRange(1, 3, lastRow, 1)) // Time
    .addRange(sheet.getRange(1, 6, lastRow, 1)) // Wereng Count
    .setPosition(2, 20, 0, 0)
    .setOption('title', 'Wereng Detection Count')
    .setOption('width', 600)
    .setOption('height', 400)
    .build();
  
  sheet.insertChart(chart2);
  
  Logger.log("Charts created successfully");
}

// Function untuk export data sebagai CSV
function exportDataAsCSV() {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(SHEET_NAME);
  var data = sheet.getDataRange().getValues();
  
  var csv = "";
  data.forEach(function(row) {
    csv += row.join(",") + "\n";
  });
  
  var fileName = "Wereng_Data_" + Utilities.formatDate(new Date(), "GMT+7", "yyyyMMdd_HHmmss") + ".csv";
  
  // Save to Google Drive
  var file = DriveApp.createFile(fileName, csv, MimeType.CSV);
  Logger.log("CSV exported: " + file.getUrl());
  
  return file.getUrl();
}

// Function untuk statistik harian
function getDailyStatistics() {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(SHEET_NAME);
  var data = sheet.getRange(2, 2, sheet.getLastRow() - 1, 5).getValues();
  
  var stats = {};
  
  data.forEach(function(row) {
    var date = row[0]; // Date column
    var temp = row[2]; // Temperature
    var humidity = row[3]; // Humidity
    var wereng = row[4]; // Wereng count
    
    if (!stats[date]) {
      stats[date] = {
        temps: [],
        humidities: [],
        werengs: [],
        count: 0
      };
    }
    
    stats[date].temps.push(temp);
    stats[date].humidities.push(humidity);
    stats[date].werengs.push(wereng);
    stats[date].count++;
  });
  
  // Calculate averages
  var summary = [];
  for (var date in stats) {
    var avgTemp = stats[date].temps.reduce((a, b) => a + b, 0) / stats[date].count;
    var avgHumidity = stats[date].humidities.reduce((a, b) => a + b, 0) / stats[date].count;
    var totalWereng = stats[date].werengs.reduce((a, b) => a + b, 0);
    
    summary.push({
      date: date,
      avgTemp: avgTemp.toFixed(1),
      avgHumidity: avgHumidity.toFixed(1),
      totalWereng: totalWereng,
      dataPoints: stats[date].count
    });
  }
  
  Logger.log(summary);
  return summary;
}
