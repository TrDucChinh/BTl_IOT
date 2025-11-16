function doGet(e) {
  const action = e.parameter.action || "";

  switch (action) {
    case "getClasses":      return getClasses();
    case "getStudents":     return getStudents(e);
    case "registerFinger":  return registerFinger(e);
    case "logAttendance":   return logAttendance(e);
    case "start":           return startClass(e);
    case "stop":            return stopClass(e);
    case "auto":            return autoUpdate(e);
    case "changeMode":      return updateMode(e);
    case "checkUpdate":     return checkUpdate(e);
    case "getAttendance":   return getAttendance(e);
    default:
      return ContentService.createTextOutput("INVALID_REQUEST");
  }
}

/* ============================================================
    SHEET HELPERS — hỗ trợ CA + ngày
============================================================ */
function _getTodaySheetName(classID, ca) {
  const now = new Date();
  const yyyy = now.getFullYear();
  const mm   = ("0" + (now.getMonth() + 1)).slice(-2);
  const dd   = ("0" + now.getDate()).slice(-2);

  return `${classID}-${ca}-${yyyy}-${mm}-${dd}`;
}

function _getTodaySheet(classID, ca) {
  const ss = SpreadsheetApp.getActive();
  return ss.getSheetByName(_getTodaySheetName(classID, ca));
}

function _createTodaySheet(classID, ca) {
  const ss = SpreadsheetApp.getActive();
  const name = _getTodaySheetName(classID, ca);

  let sh = ss.getSheetByName(name);
  if (!sh) sh = ss.insertSheet(name);

  sh.clear();
  sh.appendRow(["FingerID", "StudentID", "Name", "CheckIn", "CheckOut"]);
  sh.getRange("D:E").setNumberFormat("yyyy-MM-dd HH:mm:ss");

  return sh;
}

function getTotalStudents(classID) {
  const sh = SpreadsheetApp.getActive().getSheetByName("Students");
  const rows = sh.getDataRange().getValues();
  rows.shift();

  return rows.filter(r => r[0] == classID).length;
}

/* ============================================================
    START CLASS — CHỈ RESET sheet nếu REGIS hoặc CHECK-IN
============================================================ */
function startClass(e) {
  const classID = e.parameter.class;
  const ca      = e.parameter.ca;

  const sh = SpreadsheetApp.getActive().getSheetByName("Classes");
  const rows = sh.getDataRange().getValues();
  let mode = "";

  for (let i = 1; i < rows.length; i++) {
    const isTarget = rows[i][0] === classID;
    sh.getRange(i + 1, 4).setValue(isTarget);

    if (isTarget) mode = rows[i][2];
  }

  if (mode === "Register" || mode === "Attendance") {
    _createTodaySheet(classID, ca);
  }

  return ContentService
    .createTextOutput(JSON.stringify({ ok: true, classID, ca }))
    .setMimeType(ContentService.MimeType.JSON);
}

/* ============================================================
    STOP CLASS — CÓ QUY TẮC CHUYỂN MODE
============================================================ */
function stopClass(e) {
  const classID = e.parameter.class;

  const sh = SpreadsheetApp.getActive().getSheetByName("Classes");
  const rows = sh.getDataRange().getValues();

  for (let i = 1; i < rows.length; i++) {
    if (rows[i][0] === classID) {

      const current = rows[i][2];
      let next = "";

      if (current === "Register")      next = "Checkout";
      else if (current === "Checkout") next = "Attendance";
      else if (current === "Attendance") next = "Checkout";
      else next = "Register";

      sh.getRange(i + 1, 3).setValue(next);
      sh.getRange(i + 1, 4).setValue(false); // Start = false

      break;
    }
  }

  return ContentService.createTextOutput("STOP_OK");
}

/* ============================================================
    AUTO UPDATE MODE (ESP32)
============================================================ */
function autoUpdate(e) {
  const classID = e.parameter.class;
  const mode    = e.parameter.mode;

  const sh = SpreadsheetApp.getActive().getSheetByName("Classes");
  const rows = sh.getDataRange().getValues();

  for (let i = 1; i < rows.length; i++) {
    if (rows[i][0] === classID) {

      sh.getRange(i + 1, 3).setValue(mode);
      sh.getRange(i + 1, 4).setValue(false);

      break;
    }
  }

  return ContentService.createTextOutput("AUTO_OK");
}

/* ============================================================
    UPDATE MODE (WEB)
============================================================ */
function updateMode(e) {
  const classID = e.parameter.class;
  const mode    = e.parameter.mode;

  const sh = SpreadsheetApp.getActive().getSheetByName("Classes");
  const rows = sh.getDataRange().getValues();

  for (let i = 1; i < rows.length; i++) {
    if (rows[i][0] === classID) {

      sh.getRange(i + 1, 3).setValue(mode);
      sh.getRange(i + 1, 4).setValue(false); // khi đổi mode → STOP ngay

      break;
    }
  }

  return ContentService.createTextOutput("MODE_OK");
}

/* ============================================================
    GET CLASSES
============================================================ */
function getClasses() {
  const sh   = SpreadsheetApp.getActive().getSheetByName("Classes");
  const data = sh.getDataRange().getValues();
  const head = data.shift();

  return ContentService
    .createTextOutput(JSON.stringify(
      data.map(r => {
        let o = {};
        head.forEach((h, i) => o[h] = r[i]);
        return o;
      })
    ))
    .setMimeType(ContentService.MimeType.JSON);
}

/* ============================================================
    GET STUDENTS
============================================================ */
function getStudents(e) {
  const classID = e.parameter.class;

  const sh   = SpreadsheetApp.getActive().getSheetByName("Students");
  const rows = sh.getDataRange().getValues();
  rows.shift();

  return ContentService
    .createTextOutput(JSON.stringify(
      rows.filter(r => r[0] == classID).map(r => ({
        ClassID: r[0],
        FingerID: r[1] === "" ? "" : Number(r[1]),
        StudentID: r[2],
        Name: r[3]
      }))
    ))
    .setMimeType(ContentService.MimeType.JSON);
}

/* ============================================================
    REGISTER FINGER
============================================================ */
function registerFinger(e) {
  const { classID, fingerID, studentID } = e.parameter;

  const sh   = SpreadsheetApp.getActive().getSheetByName("Students");
  const rows = sh.getDataRange().getValues();

  for (let i = 1; i < rows.length; i++) {
    if (rows[i][0] == classID && rows[i][2] == studentID) {
      sh.getRange(i + 1, 2).setValue(Number(fingerID));
      return ContentService.createTextOutput("REGISTER_OK");
    }
  }

  return ContentService.createTextOutput("NOT_FOUND");
}

/* ============================================================
    LOG ATTENDANCE — ESP tạo sheet nếu chưa có
============================================================ */
function logAttendance(e) {
  const { classID, ca, fingerID, studentID, name, event } = e.parameter;

  let sheet = _getTodaySheet(classID, ca);
  if (!sheet) sheet = _createTodaySheet(classID, ca);

  const rows = sheet.getDataRange().getValues();

  for (let i = 1; i < rows.length; i++) {

    if (rows[i][0] == fingerID) {

      if (event === "Check-In" && rows[i][3] === "")
        sheet.getRange(i + 1, 4).setValue(new Date());

      if (event === "Check-Out") {
        if (rows[i][3] === "") return ContentService.createTextOutput("NO_CHECKIN");
        if (rows[i][4] === "") sheet.getRange(i + 1, 5).setValue(new Date());
      }

      return ContentService.createTextOutput("OK");
    }
  }

  if (event === "Check-In") {
    sheet.appendRow([fingerID, studentID, name, new Date(), ""]);
    return ContentService.createTextOutput("NEW_IN");
  }

  return ContentService.createTextOutput("IGNORE");
}

/* ============================================================
    CHECK UPDATE
============================================================ */
function checkUpdate(e) {
  const classID = e.parameter.class;
  const ca      = e.parameter.ca;

  const total = getTotalStudents(classID);

  const sheet = _getTodaySheet(classID, ca);
  if (!sheet) {
    return ContentService
      .createTextOutput(JSON.stringify({
        total,
        checkedIn: 0,
        checkedOut: 0,
        leftToCheckIn: total,
        leftToCheckOut: 0
      }))
      .setMimeType(ContentService.MimeType.JSON);
  }

  const rows = sheet.getDataRange().getValues();
  rows.shift();

  let checkedIn  = rows.filter(r => r[3] !== "").length;
  let checkedOut = rows.filter(r => r[4] !== "").length;

  return ContentService
    .createTextOutput(JSON.stringify({
      total,
      checkedIn,
      checkedOut,
      leftToCheckIn: total - checkedIn,
      leftToCheckOut: checkedIn - checkedOut
    }))
    .setMimeType(ContentService.MimeType.JSON);
}

/* ============================================================
    GET ATTENDANCE LIST
============================================================ */
function getAttendance(e) {
  const classID = e.parameter.class;
  const ca      = e.parameter.ca;

  const sheet = _getTodaySheet(classID, ca);
  if (!sheet) return ContentService.createTextOutput("[]");

  const rows = sheet.getDataRange().getValues();
  rows.shift();

  return ContentService
    .createTextOutput(JSON.stringify(
      rows.map(r => ({
        FingerID: r[0],
        StudentID: r[1],
        Name: r[2],
        CheckIn: r[3],
        CheckOut: r[4]
      }))
    ))
    .setMimeType(ContentService.MimeType.JSON);
}
