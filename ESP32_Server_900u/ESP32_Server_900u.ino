#include <FS.h>
#include "WiFi.h"
#include "ArduinoNvs.h"  // try to us nvs to replace config.ini  https://github.com/rpolitex/ArduinoNvs
#include "ESPAsyncWebSrv.h"
#include <Ticker.h>
#include "ESPAsyncDNSServer.h"  //https://github.com/devyte/ESPAsyncDNSServer
#include "esp_task_wdt.h"
#include <ESPmDNS.h>
#include <Update.h>
#include "USB.h"
#include "USBMSC.h"
#include "exfathax.h"

// enable internal goldhen.h [ true / false ]
#define INTHEN true  // goldhen is placed in the app partition to free up space on the storage for other payloads. \
                     // with this enabled you do not upload goldhen to the board, set this to false if you wish to upload goldhen.

// enable autohen [ true / false ]
//#define AUTOHEN false  // this will load goldhen instead of the normal index/payload selection page, use this if you only want hen and no other payloads. \
                       // you can update goldhen by uploading the goldhen payload to the board storage with the filename "goldhen.bin".

// enable fan threshold [ true / false ]
#define FANMOD true  // this will include a function to set the consoles fan ramp up temperature in °C \
                     // this will not work if the board is a esp32 and the usb control is disabled.

// enable usb drive indicator LED [ true / false ]
#define USELED true  // this will enable LED_BUILTIN when the usb storage is enabled.


//-------------------DEFAULT SETTINGS------------------//


//create access point
boolean startAP = true;
String AP_SSID = "PS4ESP32";
String AP_PASS = "123456789";
IPAddress Server_IP(10, 1, 1, 1);
IPAddress Subnet_Mask(255, 255, 255, 0);

//connect to wifi
boolean connectWifi = false;
String WIFI_SSID = "Home_WIFI";
String WIFI_PASS = "password";
String WIFI_HOSTNAME = "ps4.local";

//server port
int WEB_PORT = 80;

//Auto Usb Wait(milliseconds)
int USB_WAIT = 5000;

// Displayed firmware version
String firmwareVer = "1.00";

//ESP sleep after x minutes
boolean espSleep = true;
int TIME2SLEEP = 30;  // minutes

boolean autoHen = false;

//-----------------------------------------------------//


#include "Loader.h"
#include "Pages.h"
#include "jzip.h"

#include "SPIFFS.h"
#define FILESYS SPIFFS

#if INTHEN
#include "goldhen.h"
#endif

#if FANMOD
#include "fan.h"
#endif

AsyncDNSServer dnsServer;
AsyncWebServer server(WEB_PORT);
boolean hasEnabled = false;
boolean isFormating = false;
long enTime = 0;
int ftemp = 70;
long bootTime = 0;
File upFile;
USBMSC dev;
USBCDC USBSerial;


String split(String str, String from, String to) {
  String tmpstr = str;
  tmpstr.toLowerCase();
  from.toLowerCase();
  to.toLowerCase();
  int pos1 = tmpstr.indexOf(from);
  int pos2 = tmpstr.indexOf(to, pos1 + from.length());
  String retval = str.substring(pos1 + from.length(), pos2);
  return retval;
}

bool instr(String str, String search) {
  int result = str.indexOf(search);
  if (result == -1) {
    return false;
  }
  return true;
}

String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + " KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + " MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
  }
}

String urlencode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
    yield();
  }
  encodedString.replace("%2E", ".");
  return encodedString;
}

void sendwebmsg(AsyncWebServerRequest *request, String htmMsg) {
  String tmphtm = "<!DOCTYPE html><html><head><link rel=\"stylesheet\" href=\"style.css\"></head><center><br><br><br><br><br><br>" + htmMsg + "</center></html>";
  request->send(200, "text/html", tmphtm);
}


void handleFwUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    String path = request->url();
    if (path != "/update.html") {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.equals("fwupdate.bin")) {
      sendwebmsg(request, "Invalid update file: " + filename);
      return;
    }
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    //HWSerial.printf("Update Start: %s\n", filename.c_str());
    if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      sendwebmsg(request, "Update Failed: " + String(Update.errorString()));
    }
  }
  if (final) {
    if (Update.end(true)) {
      //HWSerial.printf("Update Success: %uB\n", index+len);
      String tmphtm = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}</style></head><center><br><br><br><br><br><br>Update Success, Rebooting.</center></html>";
      request->send(200, "text/html", tmphtm);
      delay(1000);
      ESP.restart();
    } else {
      Update.printError(Serial);
    }
  }
}


void handleDelete(AsyncWebServerRequest *request) {
  if (!request->hasParam("file", true)) {
    request->redirect("/fileman.html");
    return;
  }
  String path = request->getParam("file", true)->value();
  if (path.length() == 0) {
    request->redirect("/fileman.html");
    return;
  }
  if (FILESYS.exists("/" + path) && path != "/" && !path.equals("config.ini")) {
    FILESYS.remove("/" + path);
  }
  request->redirect("/fileman.html");
}

void handleFileMan(AsyncWebServerRequest *request) {
  File dir = FILESYS.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>文件管理器</title><link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;} th{border: 1px solid #dddddd; background-color:gray;padding: 8px;}</style><script>function statusDel(fname) {var answer = confirm(\"Are you sure you want to delete \" + fname + \" ?\");if (answer) {return true;} else { return false; }} </script></head><body><br><table id=filetable></table><script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("|", "%7C");
      fname.replace("\"", "%22");
      output += "\"" + fname + "|" + formatBytes(file.size()) + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script><center>No files found<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\"><u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "];var output = \"\";filelist.forEach(function(entry) {var splF = entry.split(\"|\"); output += \"<tr>\";output += \"<td><a href=\\\"\" +  splF[0] + \"\\\">\" + splF[0] + \"</a></td>\"; output += \"<td>\" + splF[1] + \"</td>\";output += \"<td><a href=\\\"/\" + splF[0] + \"\\\" download><button type=\\\"submit\\\">Download</button></a></td>\";output += \"<td><form action=\\\"/delete\\\" method=\\\"post\\\"><button type=\\\"submit\\\" name=\\\"file\\\" value=\\\"\" + splF[0] + \"\\\" onClick=\\\"return statusDel('\" + splF[0] + \"');\\\">Delete</button></form></td>\";output += \"</tr>\";}); document.getElementById(\"filetable\").innerHTML = \"<tr><th colspan='1'><center>File Name</center></th><th colspan='1'><center>File Size</center></th><th colspan='1'><center><a href='/dlall' target='mframe'><button type='submit'>Download All</button></a></center></th><th colspan='1'><center><a href='/format.html' target='mframe'><button type='submit'>Delete All</button></a></center></th></tr>\" + output;</script></body></html>";
  }
  request->send(200, "text/html", output);
}

void handleDlFiles(AsyncWebServerRequest *request) {
  File dir = FILESYS.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Downloader</title><link rel=\"stylesheet\" href=\"style.css\"><style>body{overflow-y:auto;}</style><script type=\"text/javascript\" src=\"jzip.js\"></script><script>var filelist = [";
  int fileCount = 0;
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !file.isDirectory()) {
      fileCount++;
      fname.replace("\"", "%22");
      output += "\"" + fname + "\",";
    }
    file.close();
    esp_task_wdt_reset();
  }
  if (fileCount == 0) {
    output += "];</script></head><center>No files found to download<br>You can upload files using the <a href=\"/upload.html\" target=\"mframe\"><u>File Uploader</u></a> page.</center></p></body></html>";
  } else {
    output += "]; async function dlAll(){var zip = new JSZip();for (var i = 0; i < filelist.length; i++) {if (filelist[i] != ''){var xhr = new XMLHttpRequest();xhr.open('GET',filelist[i],false);xhr.overrideMimeType('text/plain; charset=x-user-defined'); xhr.onload = function(e) {if (this.status == 200) {zip.file(filelist[i], this.response,{binary: true});}};xhr.send();document.getElementById('fp').innerHTML = 'Adding: ' + filelist[i];await new Promise(r => setTimeout(r, 50));}}document.getElementById('gen').style.display = 'none';document.getElementById('comp').style.display = 'block';zip.generateAsync({type:'blob'}).then(function(content) {saveAs(content,'esp_files.zip');});}</script></head><body onload='setTimeout(dlAll,100);'><center><br><br><br><br><div id='gen' style='display:block;'><div id='loader'></div><br><br>Generating ZIP<br><p id='fp'></p></div><div id='comp' style='display:none;'><br><br><br><br>Complete<br><br>Downloading: esp_files.zip</div></center></body></html>";
  }
  request->send(200, "text/html", output);
}


void handlePayloads(AsyncWebServerRequest *request) {
  File dir = FILESYS.open("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>ESP Server</title><link rel=\"stylesheet\" href=\"style.css\"><style>body { background-color: #1451AE; color: #ffffff; font-size: 14px; font-weight: bold; margin: 0 0 0 0.0; overflow-y:hidden; text-shadow: 3px 2px DodgerBlue;}</style><script>function setpayload(payload,title,waittime){ sessionStorage.setItem('payload', payload); sessionStorage.setItem('title', title); sessionStorage.setItem('waittime', waittime);  window.open('loader.html', '_self');}</script></head><body><center><h1>9.00 Payloads</h1>";
  int cntr = 0;
  int payloadCount = 0;
  if (USB_WAIT < 5000) { USB_WAIT = 5000; }  // correct unrealistic timing values
  if (USB_WAIT > 25000) { USB_WAIT = 25000; }

  #if INTHEN
    payloadCount++;
    cntr++;
    output += "<a onclick=\"setpayload('goldhen.bin','" + String(INTHEN_NAME) + "','" + String(USB_WAIT) + "')\"><button class=\"btn\">" + String(INTHEN_NAME) + "</button></a>&nbsp;";
  #endif

  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.endsWith(".gz")) {
      fname = fname.substring(0, fname.length() - 3);
    }
    if (fname.length() > 0 && fname.endsWith(".bin") && !file.isDirectory()) {
      payloadCount++;
      String fnamev = fname;
      fnamev.replace(".bin", "");
      output += "<a onclick=\"setpayload('" + urlencode(fname) + "','" + fnamev + "','" + String(USB_WAIT) + "')\"><button class=\"btn\">" + fnamev + "</button></a>&nbsp;";
      cntr++;
      if (cntr == 4) {
        cntr = 0;
        output += "<p></p>";
      }
    }
    file.close();
    esp_task_wdt_reset();
  }

  #if FANMOD
    payloadCount++;
    output += "<br><p><a onclick='setfantemp()'><button class='btn'>Set Fan Threshold</button></a><select id='temp' class='slct'></select></p><script>function setfantemp(){var e = document.getElementById('temp');var temp = e.value;var xhr = new XMLHttpRequest();xhr.open('POST', 'setftemp', true);xhr.onload = function(e) {if (this.status == 200) {sessionStorage.setItem('payload', 'fant.bin'); sessionStorage.setItem('title', 'Fan Temp ' + temp + ' &deg;C'); localStorage.setItem('temp', temp); sessionStorage.setItem('waittime', '10000');  window.open('loader.html', '_self');}};xhr.send('temp=' + temp);}var stmp = localStorage.getItem('temp');if (!stmp){stmp = 70;}for(var i=55; i<=85; i=i+5){var s = document.getElementById('temp');var o = document.createElement('option');s.options.add(o);o.text = i + String.fromCharCode(32,176,67);o.value = i;if (i == stmp){o.selected = true;}}</script>";
  #endif

  if (payloadCount == 0) {
    output += "<msg>No .bin payloads found<br>You need to upload the payloads to the ESP32 board.<br>in the arduino ide select <b>Tools</b> &gt; <b>ESP32 Sketch Data Upload</b><br>or<br>Using a pc/laptop connect to <b>" + AP_SSID + "</b> and navigate to <a href=\"/admin.html\"><u>http://" + WIFI_HOSTNAME + "/admin.html</u></a> and upload the .bin payloads using the <b>File Uploader</b></msg></center></body></html>";
  }
  output += "</center></body></html>";
  request->send(200, "text/html", output);
}


void handleConfig(AsyncWebServerRequest *request) {
  if (request->hasParam("ap_ssid", true) && request->hasParam("ap_pass", true) && request->hasParam("web_ip", true) && request->hasParam("web_port", true) && request->hasParam("subnet", true) && request->hasParam("wifi_ssid", true) && request->hasParam("wifi_pass", true) && request->hasParam("wifi_host", true) && request->hasParam("usbwait", true)) {
    NVS.setString("AP_SSID", request->getParam("ap_ssid", true)->value());
    NVS.setString("AP_PASS", request->getParam("ap_pass", true)->value().equals("********") ? AP_PASS : request->getParam("ap_pass", true)->value());
    NVS.setString("WEBSERVER_IP", request->getParam("web_ip", true)->value());
    NVS.setString("WEBSERVER_PORT", request->getParam("web_port", true)->value());
    NVS.setString("SUBNET_MASK", request->getParam("subnet", true)->value());
    NVS.setString("WIFI_SSID", request->getParam("wifi_ssid", true)->value());
    NVS.setString("WIFI_PASS", request->getParam("wifi_pass", true)->value().equals("********") ? WIFI_PASS : request->getParam("wifi_pass", true)->value());
    NVS.setString("WIFI_HOST", request->getParam("wifi_host", true)->value());
    NVS.setString("USEAP", request->hasParam("useap", true) ? "true" : "false");
    NVS.setString("CONWIFI", request->hasParam("usewifi", true) ? "true" : "false");
    NVS.setString("USBWAIT", request->getParam("usbwait", true)->value());
    NVS.setString("ESPSLEEP", request->hasParam("espsleep", true) ? "true" : "false");
    NVS.setString("SLEEPTIME", request->getParam("sleeptime", true)->value());
    NVS.setString("AUTOHEN", request->hasParam("autohen", true) ? "true" : "false");
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">#loader {z-index: 1;width: 50px;height: 50px;margin: 0 0 0 0;border: 6px solid #f3f3f3;border-radius: 50%;border-top: 6px solid #3498db;width: 50px;height: 50px;-webkit-animation: spin 2s linear infinite;animation: spin 2s linear infinite; } @-webkit-keyframes spin {0%{-webkit-transform: rotate(0deg);}100%{-webkit-transform: rotate(360deg);}}@keyframes spin{0%{ transform: rotate(0deg);}100%{transform: rotate(360deg);}}body {background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;} #msgfmt {font-size: 16px; font-weight: normal;}#status {font-size: 16px; font-weight: normal;}</style></head><center><br><br><br><br><br><p id=\"status\"><div id='loader'></div><br>配置已保存<br>正在重启</p></center></html>";
    request->send(200, "text/html", htmStr);
    delay(1000);
    ESP.restart();
  } else {
    request->redirect("/config.html");
  }
}


void handleReboot(AsyncWebServerRequest *request) {
  //HWSerial.print("Rebooting ESP");
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", rebooting_gz, sizeof(rebooting_gz));
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
  delay(1000);
  ESP.restart();
}


void handleConfigHtml(AsyncWebServerRequest *request) {
  String tmpUa = "";
  String tmpCw = "";
  String tmpSlp = "";
  String tmpAh = "";
  if (startAP) { tmpUa = "checked"; }
  if (connectWifi) { tmpCw = "checked"; }
  if (espSleep) { tmpSlp = "checked"; }
  if (autoHen) { tmpAh = "checked"; }

  String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title><style type=\"text/css\">body {background-color: #1451AE; color: #ffffff; font-size: 14px;font-weight: bold;margin: 0 0 0 0.0;padding: 0.4em 0.4em 0.4em 0.6em;}input[type=\"submit\"]:hover {background: #ffffff;color: green;}input[type=\"submit\"]:active{outline-color: green;color: green;background: #ffffff; }table {font-family: arial, sans-serif;border-collapse: collapse;}td {border: 1px solid #dddddd;text-align: left;padding: 8px;}th {border: 1px solid #dddddd; background-color:gray;text-align: center;padding: 8px;}</style></head><body><form action=\"/config.html\" method=\"post\"><center><table><tr><th colspan=\"2\"><center>连接热点</center></th></tr><tr><td>热点SSID:</td><td><input name=\"ap_ssid\" value=\"" + AP_SSID + "\"></td></tr><tr><td>热点密码:</td><td><input name=\"ap_pass\" value=\"********\"></td></tr><tr><td>热点IP:</td><td><input name=\"web_ip\" value=\"" + Server_IP.toString() + "\"></td></tr><tr><td>子网掩码:</td><td><input name=\"subnet\" value=\"" + Subnet_Mask.toString() + "\"></td></tr><tr><td>开启热点:</td><td><input type=\"checkbox\" name=\"useap\" " + tmpUa + "></td></tr><tr><th colspan=\"2\"><center>Web服务器</center></th></tr><tr><td>服务器端口:</td><td><input name=\"web_port\" value=\"" + String(WEB_PORT) + "\"></td></tr><tr><th colspan=\"2\"><center>连接WIFI</center></th></tr><tr><td>WIFI SSID:</td><td><input name=\"wifi_ssid\" value=\"" + WIFI_SSID + "\"></td></tr><tr><td>WIFI密码:</td><td><input name=\"wifi_pass\" value=\"********\"></td></tr><tr><td>主机名:</td><td><input name=\"wifi_host\" value=\"" + WIFI_HOSTNAME + "\"></td></tr><tr><td>连接WIFI:</td><td><input type=\"checkbox\" name=\"usewifi\" " + tmpCw + "></td></tr><tr><th colspan=\"2\"><center>自动USB</center></th></tr><tr><td>等待时长(ms):</td><td><input name=\"usbwait\" value=\"" + USB_WAIT + "\"></td></tr><tr><th colspan=\"2\"><center>自动加载HEN</center></th></tr><tr><td>开启自动HEN(自动注入goldhen.bin):</td><td><input type=\"checkbox\" name=\"autohen\" " + tmpAh + "></td></tr><tr><th colspan=\"2\"><center>ESP睡眠功能</center></th></tr><tr><td>开启睡眠:</td><td><input  type=\"checkbox\" name=\"espsleep\" " + tmpSlp + "></td></tr><tr><td>睡眠定时(minutes):</td><td><input name=\"sleeptime\" value=\"" + TIME2SLEEP + "\"></td></tr></table><br><input id=\"savecfg\" type=\"submit\" value=\"保存配置\"></center></form></body></html>";
  request->send(200, "text/html", htmStr);
}


void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    String path = request->url();
    if (path != "/upload.html") {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    if (filename.equals("/config.ini")) { return; }
    //HWSerial.printf("Upload Start: %s\n", filename.c_str());
    upFile = FILESYS.open(filename, "w");
  }
  if (upFile) {
    upFile.write(data, len);
  }
  if (final) {
    upFile.close();
    //HWSerial.printf("upload Success: %uB\n", index+len);
  }
}


void handleConsoleUpdate(String rgn, AsyncWebServerRequest *request) {
  String Version = "05.050.000";
  String sVersion = "05.050.000";
  String lblVersion = "5.05";
  String imgSize = "0";
  String imgPath = "";
  String xmlStr = "<?xml version=\"1.0\" ?><update_data_list><region id=\"" + rgn + "\"><force_update><system level0_system_ex_version=\"0\" level0_system_version=\"" + Version + "\" level1_system_ex_version=\"0\" level1_system_version=\"" + Version + "\"/></force_update><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"><update_data update_type=\"full\"><image size=\"" + imgSize + "\">" + imgPath + "</image></update_data></system_pup><recovery_pup type=\"default\"><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"/><image size=\"" + imgSize + "\">" + imgPath + "</image></recovery_pup></region></update_data_list>";
  request->send(200, "text/xml", xmlStr);
}


void handleCacheManifest(AsyncWebServerRequest *request) {
  String output = "CACHE MANIFEST\r\n";
  File dir = FILESYS.open("/");
  while (dir) {
    File file = dir.openNextFile();
    if (!file) {
      dir.close();
      break;
    }
    String fname = String(file.name());
    if (fname.length() > 0 && !fname.equals("config.ini") && !file.isDirectory()) {
      if (fname.endsWith(".gz")) {
        fname = fname.substring(0, fname.length() - 3);
      }
      output += urlencode(fname) + "\r\n";
    }
    file.close();
  }
  if (!instr(output, "index.html\r\n")) {
    output += "index.html\r\n";
  }
  if (!instr(output, "menu.html\r\n")) {
    output += "menu.html\r\n";
  }
  if (!instr(output, "loader.html\r\n")) {
    output += "loader.html\r\n";
  }
  if (!instr(output, "payloads.html\r\n")) {
    output += "payloads.html\r\n";
  }
  if (!instr(output, "style.css\r\n")) {
    output += "style.css\r\n";
  }
  #if INTHEN
    if (!instr(output, "goldhen.bin\r\n")) {
      output += "goldhen.bin\r\n";
    }
  #endif
  request->send(200, "text/cache-manifest", output);
}


void handleInfo(AsyncWebServerRequest *request) {
  float flashFreq = (float)ESP.getFlashChipSpeed() / 1000.0 / 1000.0;
  FlashMode_t ideMode = ESP.getFlashChipMode();
  String mcuType = CONFIG_IDF_TARGET;
  mcuType.toUpperCase();
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>System Information</title><link rel=\"stylesheet\" href=\"style.css\"></head>";
  output += "<hr>###### Software ######<br><br>";
  output += "Firmware version " + firmwareVer + "<br>";
  output += "SDK version: " + String(ESP.getSdkVersion()) + "<br><hr>";
  output += "###### Board ######<br><br>";
  output += "MCU: " + mcuType + "<br>";
  #if defined(USB_PRODUCT)
    output += "Board: " + String(USB_PRODUCT) + "<br>";
  #endif
    output += "Chip Id: " + String(ESP.getChipModel()) + "<br>";
    output += "CPU frequency: " + String(ESP.getCpuFreqMHz()) + "MHz<br>";
    output += "Cores: " + String(ESP.getChipCores()) + "<br><hr>";
    output += "###### Flash chip information ######<br><br>";
    output += "Flash chip Id: " + String(ESP.getFlashChipMode()) + "<br>";
    output += "Estimated Flash size: " + formatBytes(ESP.getFlashChipSize()) + "<br>";
    output += "Flash frequency: " + String(flashFreq) + " MHz<br>";
    output += "Flash write mode: " + String((ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT"
                                                                      : ideMode == FM_DIO  ? "DIO"
                                                                      : ideMode == FM_DOUT ? "DOUT"
                                                                                            : "UNKNOWN"))
              + "<br><hr>";
    output += "###### Storage information ######<br><br>";
    output += "Filesystem: SPIFFS<br>";
    output += "Total Size: " + formatBytes(FILESYS.totalBytes()) + "<br>";
    output += "Used Space: " + formatBytes(FILESYS.usedBytes()) + "<br>";
    output += "Free Space: " + formatBytes(FILESYS.totalBytes() - FILESYS.usedBytes()) + "<br><hr>";
  #if defined(CONFIG_IDF_TARGET_ESP32S2) | defined(CONFIG_IDF_TARGET_ESP32S3)
    if (ESP.getPsramSize() > 0) {
      output += "###### PSRam information ######<br><br>";
      output += "Psram Size: " + formatBytes(ESP.getPsramSize()) + "<br>";
      output += "Free psram: " + formatBytes(ESP.getFreePsram()) + "<br>";
      output += "Max alloc psram: " + formatBytes(ESP.getMaxAllocPsram()) + "<br><hr>";
    }
  #endif
  output += "###### Ram information ######<br><br>";
  output += "Ram size: " + formatBytes(ESP.getHeapSize()) + "<br>";
  output += "Free ram: " + formatBytes(ESP.getFreeHeap()) + "<br>";
  output += "Max alloc ram: " + formatBytes(ESP.getMaxAllocHeap()) + "<br><hr>";
  output += "###### Sketch information ######<br><br>";
  output += "Sketch hash: " + ESP.getSketchMD5() + "<br>";
  output += "Sketch size: " + formatBytes(ESP.getSketchSize()) + "<br>";
  output += "Free space available: " + formatBytes(ESP.getFreeSketchSpace() - ESP.getSketchSize()) + "<br><hr>";
  output += "</html>";
  request->send(200, "text/html", output);
}

void writeNVS() {
  NVS.setString("stored", "true");
  String tmpua = "false";
  String tmpcw = "false";
  String tmpslp = "false";
  String tmpah = "false";
  if (startAP) { tmpua = "true"; }
  if (connectWifi) { tmpcw = "true"; }
  if (espSleep) { tmpslp = "true"; }
  if (autoHen) { tmpah = "true"; }
  NVS.setString("AP_SSID", AP_SSID);
  NVS.setString("AP_PASS", AP_PASS);
  NVS.setString("WEBSERVER_IP", Server_IP.toString());
  NVS.setString("WEBSERVER_PORT", String(WEB_PORT));
  NVS.setString("SUBNET_MASK", Subnet_Mask.toString());
  NVS.setString("WIFI_SSID", WIFI_SSID);
  NVS.setString("WIFI_PASS", WIFI_PASS);
  NVS.setString("WIFI_HOST", WIFI_HOSTNAME);
  NVS.setString("USEAP", tmpua);
  NVS.setString("CONWIFI", tmpcw);
  NVS.setString("USBWAIT", String(USB_WAIT));
  NVS.setString("ESPSLEEP", tmpslp);
  NVS.setString("SLEEPTIME", String(TIME2SLEEP));
  NVS.setString("AUTOHEN", tmpah);
}

#if USELED
  Ticker led_ticker; // control when to stop blinking
  Ticker led_blinker; // control blinking pattern

  void stopBlink() {
    led_ticker.detach();
    led_blinker.detach();
    digitalWrite(LED_BUILTIN, LOW);
  }
  void blink() {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }
#endif
bool cdc_started;
/* use button 0 to start CDC */
void ARDUINO_ISR_ATTR start_cdc() {
  if (digitalRead(0) == LOW) {
    USBSerial.begin();
    cdc_started = true;
    USB.begin();
    #if USELED
      led_ticker.attach(2, stopBlink);
      led_blinker.attach_ms(300, blink);
    #endif
    detachInterrupt(digitalPinToInterrupt(0));
  }
}

void setup() {
  #if USELED
    pinMode(LED_BUILTIN, OUTPUT);
  #endif
  cdc_started = false;
  pinMode(0, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(0), start_cdc, FALLING);
  //HWSerial.begin(115200);
  //HWSerial.println("Version: " + firmwareVer);
  //USBSerial.begin();
  FILESYS.begin(true);
  if (NVS.begin()) {
    if (NVS.getString("stored") != "") {
      if (NVS.getString("AP_SSID") != "") {
        AP_SSID = NVS.getString("AP_SSID");
      }
      if (NVS.getString("AP_PASS") != "") {
        AP_PASS = NVS.getString("AP_PASS");
      }
      if (NVS.getString("WEBSERVER_IP") != "") {
        Server_IP.fromString(NVS.getString("WEBSERVER_IP"));
      }
      if (NVS.getString("SUBNET_MASK") != "") {
        Subnet_Mask.fromString(NVS.getString("SUBNET_MASK"));
      }
      if (NVS.getString("WIFI_SSID") != "") {
        WIFI_SSID = NVS.getString("WIFI_SSID");
      }
      if (NVS.getString("WIFI_PASS") != "") {
        WIFI_PASS = NVS.getString("WIFI_PASS");
      }
      if (NVS.getString("WIFI_HOST") != "") {
        WIFI_HOSTNAME = NVS.getString("WIFI_HOST");
      }
      if (NVS.getString("USEAP") != "") {
        String strua = NVS.getString("USEAP");
        if (strua.equals("true")) {
          startAP = true;
        } else {
          startAP = false;
        }
      }
      if (NVS.getString("CONWIFI") != "") {
        String strcw = NVS.getString("CONWIFI");
        if (strcw.equals("true")) {
          connectWifi = true;
        } else {
          connectWifi = false;
          startAP = true;
        }
      }
      if (NVS.getString("USBWAIT") != "") {
        USB_WAIT = NVS.getString("USBWAIT").toInt();
      }
      if (NVS.getString("AUTOHEN") != "") {
        String strah = NVS.getString("AUTOHEN");
        if (strah.equals("true")) {
          autoHen = true;
        } else {
          autoHen = false;
        }
      }
      if (NVS.getString("ESPSLEEP") != "") {
        String strsl = NVS.getString("ESPSLEEP");
        if (strsl.equals("true")) {
          espSleep = true;
        } else {
          espSleep = false;
        }
      }
      if (NVS.getString("SLEEPTIME") != "") {
        TIME2SLEEP = NVS.getString("SLEEPTIME").toInt();
      }
    } else {
      writeNVS();
    }
  } else {
    //HWSerial.println("Failed to load nvs");
  }
  if (startAP) {
    //HWSerial.println("SSID: " + AP_SSID);
    //HWSerial.println("Password: " + AP_PASS);
    //HWSerial.println("");
    //HWSerial.println("WEB Server IP: " + Server_IP.toString());
    //HWSerial.println("Subnet: " + Subnet_Mask.toString());
    //HWSerial.println("WEB Server Port: " + String(WEB_PORT));
    //HWSerial.println("");
    WiFi.softAPConfig(Server_IP, Server_IP, Subnet_Mask);
    WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
    //HWSerial.println("WIFI AP started");
    dnsServer.setTTL(30);
    dnsServer.setErrorReplyCode(AsyncDNSReplyCode::ServerFailure);
    dnsServer.start(53, "*", Server_IP);
    //HWSerial.println("DNS server started");
    //HWSerial.println("DNS Server IP: " + Server_IP.toString());
  }
  if (connectWifi && WIFI_SSID.length() > 0 && WIFI_PASS.length() > 0) {
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.hostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
    //HWSerial.println("WIFI connecting");
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      //HWSerial.println("Wifi failed to connect");
    } else {
      IPAddress LAN_IP = WiFi.localIP();
      if (LAN_IP) {
        //HWSerial.println("Wifi Connected");
        //HWSerial.println("WEB Server LAN IP: " + LAN_IP.toString());
        //HWSerial.println("WEB Server Port: " + String(WEB_PORT));
        //HWSerial.println("WEB Server Hostname: " + WIFI_HOSTNAME);
        String mdnsHost = WIFI_HOSTNAME;
        mdnsHost.replace(".local", "");
        MDNS.begin(mdnsHost.c_str());
        if (!startAP) {
          dnsServer.setTTL(30);
          dnsServer.setErrorReplyCode(AsyncDNSReplyCode::ServerFailure);
          dnsServer.start(53, "*", LAN_IP);
          //HWSerial.println("DNS server started");
          //HWSerial.println("DNS Server IP: " + LAN_IP.toString());
        }
      }
    }
  }
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Microsoft Connect Test");
  });

  server.on("/cache.manifest", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleCacheManifest(request);
  });

  server.on("/config.ini", HTTP_ANY, [](AsyncWebServerRequest *request) {
    request->send(404);
  });

  server.on("/upload.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", upload_gz, sizeof(upload_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on(
    "/upload.html", HTTP_POST, [](AsyncWebServerRequest *request) {
      request->redirect("/fileman.html");
    },
    handleFileUpload);

  server.on("/fileman.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleFileMan(request);
  });

  server.on("/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleDelete(request);
  });

  server.on("/config.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleConfigHtml(request);
  });

  server.on("/config.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleConfig(request);
  });

  server.on("/admin.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", admin_gz, sizeof(admin_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/reboot.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", reboot_gz, sizeof(reboot_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/reboot.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleReboot(request);
  });

  server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", update_gz, sizeof(update_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on(
    "/update.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    },
    handleFwUpdate);

  server.on("/info.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleInfo(request);
  });

  server.on("/usbon", HTTP_POST, [](AsyncWebServerRequest *request) {
    enableUSB();
    request->send(200, "text/plain", "ok");
  });

  server.on("/usboff", HTTP_POST, [](AsyncWebServerRequest *request) {
    disableUSB();
    request->send(200, "text/plain", "ok");
  });

  server.on("/format.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", format_gz, sizeof(format_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.on("/format.html", HTTP_POST, [](AsyncWebServerRequest *request) {
    isFormating = true;
    request->send(304);
  });

  server.on("/dlall", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleDlFiles(request);
  });

  server.on("/jzip.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/javascript", jzip_gz, sizeof(jzip_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  #if FANMOD
    server.on("/setftemp", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (request->hasParam("temp", true)) {
        ftemp = request->getParam("temp", true)->value().toInt();
        request->send(200, "text/plain", "ok");
      } else {
        request->send(404);
      }
    });

    server.on("/fant.bin", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (ftemp < 55 || ftemp > 85) { ftemp = 70; }
      uint8_t *fant = (uint8_t *)malloc(sizeof(uint8_t) * sizeof(fan));
      memcpy_P(fant, fan, sizeof(fan));
      fant[250] = ftemp;
      fant[368] = ftemp;
      AsyncWebServerResponse *response = request->beginResponse_P(200, "application/octet-stream", fant, sizeof(fan));
      request->send(response);
      free(fant);
    });
  #endif

  server.serveStatic("/", FILESYS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    //HWSerial.println(request->url());
    String path = request->url();
    if (instr(path, "/update/ps4/")) {
      String Region = split(path, "/update/ps4/list/", "/");
      handleConsoleUpdate(Region, request);
      return;
    }
    if (instr(path, "/document/") && instr(path, "/ps4/")) {
      request->redirect("http://" + WIFI_HOSTNAME + "/index.html");
      return;
    }
    if (path.endsWith("index.html") || path.endsWith("index.htm") || path.endsWith("/")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_gz, sizeof(index_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    if (path.endsWith("style.css")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", style_gz, sizeof(style_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }
    if (path.endsWith("payloads.html")) {
      if(autoHen) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", autohen_gz, sizeof(autohen_gz));
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
      } else {
        handlePayloads(request);
      }
      return;
    }
    if (path.endsWith("loader.html")) {
      AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", loader_gz, sizeof(loader_gz));
      response->addHeader("Content-Encoding", "gzip");
      request->send(response);
      return;
    }

  #if INTHEN
      if (path.endsWith("goldhen.bin")) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "application/octet-stream", goldhen_gz, sizeof(goldhen_gz));
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
        return;
      }
  #endif

    request->send(404);
  });


  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
  //HWSerial.println("HTTP server started");

  if (TIME2SLEEP < 5) { TIME2SLEEP = 5; }  //min sleep time
  bootTime = millis();
}


static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (lba > 4) { lba = 4; }
  memcpy(buffer, exfathax[lba] + offset, bufsize);
  return bufsize;
}

void enableUSB() {
  #if USELED
    digitalWrite(LED_BUILTIN, HIGH);
  #endif
  dev.vendorID("PS4");
  dev.productID("ESP32 Server");
  dev.productRevision("1.0");
  dev.onRead(onRead);
  dev.mediaPresent(true);
  dev.begin(8192, 512);
  USB.begin();
  enTime = millis();
  hasEnabled = true;
}

void disableUSB() {
  #if USELED
    digitalWrite(LED_BUILTIN, LOW);
  #endif
  enTime = 0;
  hasEnabled = false;
  dev.end();
  ESP.restart();
}


void loop() {
  if (espSleep && !isFormating) {
    if (millis() >= (bootTime + (TIME2SLEEP * 60000))) {
      //HWSerial.print("Esp sleep");
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
      esp_deep_sleep_start();
      return;
    }
  }
  if (hasEnabled && millis() >= (enTime + 15000)) {
    disableUSB();
  }
  if (isFormating) {
    //HWSerial.print("Formatting Storage");
    isFormating = false;
    FILESYS.end();
    FILESYS.format();
    FILESYS.begin(true);
    delay(1000);
  }
  // dnsServer.processNextRequest();
}
