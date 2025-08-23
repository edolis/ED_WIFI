#include "ED_wifi.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <esp_http_server.h>

// #include <cstring>
// #include "esp_wifi.h"

// #include <freertos/mpu_wrappers.h>
// #include <esp_wifi.h>
// #include <esp_log.h>

#define pdMINUTES_TO_TICKS(x) (pdMS_TO_TICKS((x) * 60 * 1000))
#define RETURN_ON_ERROR(expr, tag, msg)                                        \
  do {                                                                         \
    esp_err_t __err_rc = (expr);                                               \
    if (__err_rc != ESP_OK) {                                                  \
      ESP_LOGE(tag, "%s: 0x%x", msg, __err_rc);                                \
      return __err_rc;                                                         \
    }                                                                          \
  } while (0)

// defines two to store credentials for both 4G and 5G networks
// char ED_wifi::_SSID[2][ED_MAX_SSID_PWD_SIZE] = {{0}};
// char ED_wifi::_PWD[2][ED_MAX_SSID_PWD_SIZE] = {{0}};

namespace ED_wifi {

REGISTER_NVS_NAMESPACE(WiFiService::APCredentialManager::NVS_AREA_NAME)

/**
 * @brief Maximum number of retry attempts for WiFi operations.
 *
 * This constant defines the maximum number of times the WiFi module will
 * attempt to retry a failed operation before giving up.
 */
#ifdef DEBUG_BUILD
constexpr int MAX_RETRY = 4;
#else
constexpr int MAX_RETRY = 10;
#endif
/**
 * @brief
 */
constexpr TickType_t delay_ticks = pdMS_TO_TICKS(500);
constexpr TickType_t retry_timeout_ticks =
    pdMINUTES_TO_TICKS(15); // T_RETRY minutes

int WiFiService::s_retry_num = 0;

static const char *TAG = "ED_wifi";

const WiFiService::APCredential *WiFiService::APCredentialManager::curAP =
    nullptr;
esp_err_t WiFiService::setHostName() {
  esp_err_t qerr;
  ESP_LOGI(TAG, "starting setHostName");
  qerr = esp_wifi_get_mac(WIFI_IF_STA, WiFiService::station_mac.set());
  char macStr[18];
  WiFiService::station_mac.toString(macStr, sizeof(macStr));
  ESP_LOGI(TAG, "MAC query answers: %s newMAC: %s ", esp_err_to_name(qerr),
           macStr);

  snprintf(WiFiService::station_ID, sizeof(WiFiService::station_ID),
           "EDESP_%02X-%02X-%02X", WiFiService::station_mac[3],
           WiFiService::station_mac[4], WiFiService::station_mac[5]);
  ESP_LOGI(TAG, "MAC- Generated ID: %s", WiFiService::station_ID);
  // if (sta_netif != NULL)
  {
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, WiFiService::station_ID));
    ESP_LOGI(TAG, "Hostname set to: %s", WiFiService::station_ID);
  }
  return ESP_OK;
}

char WiFiService::station_ID[] = "ED_ESP32"; // dafault value for ESP32 hostname
WiFiService::APCredential
    WiFiService::APCredentialManager::credentials[maxTrackedSSIDs];
size_t WiFiService::APCredentialManager::count = 0;

WiFiService::APCredential::APCredential()
    : ssid{""}, password{""}, type(AP_CONNECTABLE), RSSI(0), chann(0),
      lastSeen(0) {};
WiFiService::APCredential::APCredential(const char *s, const char *p,
                                        bool canConnect)
    : ssid{""}, password{""},
      type(canConnect ? AP_CONNECTABLE : AP_UNCONNECTABLE), RSSI(0), chann(0),
      lastSeen(0) {
  strncpy(ssid, s, sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0';

  strncpy(password, p, sizeof(password) - 1);
  password[sizeof(password) - 1] = '\0';
}

bool WiFiService::APCredential::matches(const char *targetSsid) const {
  return strncmp(ssid, targetSsid, sizeof(ssid)) == 0;
}

bool WiFiService::APCredentialManager::remove(const char *ssid) {
  for (size_t i = 0; i < count; ++i) {
    if (credentials[i].matches(ssid)) {
      // Shift remaining entries
      for (size_t j = i; j < count - 1; ++j) {
        credentials[j] = credentials[j + 1];
      }
      --count;
      return true;
    }
  }
  return false;
}

const WiFiService::APCredential *
WiFiService::APCredentialManager::findAndUpdateInfo(char *ssid, int strength,
                                                    uint8_t chann,
                                                    int8_t index) {
  // checks if the suggested index actually matches the sid otherwise falls back
  // to the normal search
  int8_t idx = index;
  if (!(index >= 0 && index < count && credentials[index].matches(ssid)))
    for (size_t i = 0; i < count; ++i)
      if (credentials[i].matches(ssid)) {
        idx = i;
        break;
      }
  if (idx >= 0) {
    credentials[idx].RSSI = strength;
    credentials[idx].chann = chann;
    credentials[idx].lastSeen = esp_timer_get_time() / 1E6;
    return &credentials[idx];
  }
  return nullptr;
}
const WiFiService::APCredential *
WiFiService::APCredentialManager::retrieve(const char *ssid) {
  for (size_t i = 0; i < count; ++i) {
    if (credentials[i].matches(ssid))
      return &credentials[i];
  }
  return nullptr;
}

const WiFiService::APCredential
    *WiFiService::APCredentialManager::activeSSIDs[maxTrackedSSIDs + 1] = {
        nullptr};

void WiFiService::APCredentialManager::loadDefaultAPs() {
  if (initialized)
    return;
  // rawCredential is used to load at startup the SIID and PWD defined in the
  // Secrets.h
  count = 0;
  static const char *rawCredentials[][3] = {ED_WIFI_CREDENTIALS};
  static const size_t rawCredentialCount =
      sizeof(rawCredentials) / sizeof(rawCredentials[0]);
  for (size_t i = 0; i < rawCredentialCount && count < maxTrackedSSIDs; ++i) {
    credentials[count++] = APCredential(
        rawCredentials[i][0], rawCredentials[i][1],
        !(rawCredentials[i][2][0] == '0' || rawCredentials[i][2][0] == 'u' ||
          rawCredentials[i][2][0] == 'U'));
  }
  ESP_LOGI(TAG, "loadDefaultAPs loads %d credentials from firmware", count);
  initialized = true;
};
/*
void WiFiService::retry_sta_mode_task(void *arg)
{
    vTaskDelay(retry_timeout_ticks);

    ESP_LOGI(TAG, "Retrying STA mode after AP fallback");

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_connect();

    vTaskDelete(nullptr);
}
*/
TimerHandle_t WiFiService::staRetryTimer = nullptr;

void WiFiService::sta_retry_callback(TimerHandle_t xTimer) {
  ESP_LOGI(TAG, "Retrying STA mode from AP fallback");

  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_STA);
  // note! starting will cause a new WIFI_EVENT_STA_START event, which will
  // trigger a rescan of the Ap in range
  //  therefore there is no need to modify the logic. If nothing is in range, it
  //  will reactivate APmode.
  esp_wifi_start();
  // esp_wifi_connect(); do NOT launch connect since it will conflict with the
  // scan

  // vTaskDelete(nullptr); //delete on success, driven by event
}

void WiFiService::init_sta_retry_timer() {
  const TickType_t retry_interval = pdMS_TO_TICKS(
#ifdef DEBUG_BUILD
      5
#else
      15
#endif
      * 60 * 1000); // 15 minutes

  staRetryTimer = xTimerCreate("STA Retry Timer", retry_interval, pdTRUE,
                               nullptr, sta_retry_callback);
  // xTimerStart(staRetryTimer, 0);
  // the timer will be started by the event manager when needed.
}

/**
 * @brief switches from STA mode to AP mode as a fallback in case connection to
 * wifi fails (network failure/change of credential?).
 */
// allows a device to connect the device and potentiaqlly manually update
// credentials [to be implemented elsewhere]
esp_err_t WiFiService::wifi_conn_AP() {
  ESP_LOGI(TAG, "Switching to AP mode");
  wifi_config_t ap_config = {};
  strcpy((char *)ap_config.ap.ssid, WiFiService::station_ID);
  ap_config.ap.ssid_len = strlen(WiFiService::station_ID);
  ap_config.ap.max_connection = 4;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;

  esp_wifi_stop();
  ESP_LOGI(TAG, "Wifi stopped, stating as AP...");
  RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "AP setmode");
  RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                  "AP set config");
  RETURN_ON_ERROR(esp_wifi_start(), TAG, "AP start");

  ESP_LOGW(TAG, "Switched to AP mode");
  return ESP_OK;
}

const char *WiFiService::wifi_reason_to_string(uint8_t reason) {
  switch (reason) {
  case WIFI_REASON_UNSPECIFIED:
    return "Unspecified reason";
  case WIFI_REASON_AUTH_EXPIRE:
    return "Authentication expired";
  case WIFI_REASON_AUTH_LEAVE:
    return "Left due to authentication";
  case WIFI_REASON_ASSOC_EXPIRE:
    return "Association expired";
  case WIFI_REASON_ASSOC_TOOMANY:
    return "Too many associations";
  case WIFI_REASON_NOT_AUTHED:
    return "Not authenticated";
  case WIFI_REASON_NOT_ASSOCED:
    return "Not associated";
  case WIFI_REASON_ASSOC_LEAVE:
    return "Left association";
  case WIFI_REASON_AUTH_FAIL:
    return "Authentication failed (wrong password)";
  case WIFI_REASON_NO_AP_FOUND:
    return "Access Point not found";
  case WIFI_REASON_HANDSHAKE_TIMEOUT:
    return "Handshake timeout";
  case WIFI_REASON_BEACON_TIMEOUT:
    return "Beacon timeout";
  case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    return "4-way handshake timeout";
  default:
    return "Unknown reason";
  }
}

TimerHandle_t WiFiService::staRetryDelayed =
    xTimerCreate("ReconnectTimer", pdMS_TO_TICKS(ReconnectDelay_ms), pdFALSE,
                 NULL, reconnectCallback);

void WiFiService::reconnectCallback(TimerHandle_t xTimer) {
  esp_wifi_connect();
}

void WiFiService::event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      ESP_LOGI(TAG, "STA start completed. Scanning WiFi networks...");
      // initializes the internal station ID
      scan_wifi_networks();
      break;
    case WIFI_EVENT_SCAN_DONE:
      ESP_LOGI(TAG, "SCAN_DONE connecting...");
      APCredentialManager::setNextActiveAP();
      wifi_conn_STA();
      esp_wifi_connect();
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      wifi_event_sta_disconnected_t *disconn =
          (wifi_event_sta_disconnected_t *)event_data;
      ESP_LOGW(TAG, "A wifi disconnect event occurred. Reason: {%s}",
               wifi_reason_to_string(disconn->reason));

      if (s_retry_num++ < MAX_RETRY) {
        ESP_LOGW(TAG, "Disconnected. Retry #%d with SAME AP %s", s_retry_num,
                 APCredentialManager::curAP->ssid);
        xTimerStart(staRetryDelayed, 0);
      } else {
        // ESP_LOGI(TAG, "Disconnected. exceeded MAX_RETRY");
        bool networkAvailable = false;
        networkAvailable = APCredentialManager::setNextActiveAP();
        if (networkAvailable &&
            APCredentialManager::curAP !=
                nullptr) // curAP could be nullptr also to signal the
                         // connectable AP options have been exhausted
        {
          ESP_LOGW(TAG, "Max retries reached. Switching to SSID: (%s).",
                   APCredentialManager::curAP->ssid);
          wifi_conn_STA(); // there is an alternative valid connectable AP in
                           // reach, tries to switch to it. here, reconfigures
                           // the sta to use new AP
          esp_wifi_connect();
        } else { // no alternative or no network, switches to AP mode and
                 // schedules a retry to connect back to STA
          ESP_LOGI(TAG,
                   "%s. Switching to AP mode and scheduling retry for STA "
                   "connection.",
                   networkAvailable ? "Max retries reached"
                                    : "No Network available");
          wifi_conn_AP();
          // WebInterfaace::init(); //launches the interface to allow user to
          // add AP credential or modify existing ones
          xTimerStart(staRetryTimer, 0); //
        }
      }
      break;

      // default:
      //     break;
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Connected to {%s}, as host{%s} with IP: [" IPSTR "]",
             APCredentialManager::curAP->ssid, station_ID,
             IP2STR(&event->ip_info.ip));
    runGotIPsubscribers();
    s_retry_num = 0;
    if (staRetryTimer != nullptr) {
      xTimerStop(staRetryTimer, 0); // Stops the timer}
    }
  }
}

int WiFiService::APCredential::compare_rssi_desc(const void *a, const void *b) {
  const APCredential *apA = (const APCredential *)a;
  const APCredential *apB = (const APCredential *)b;
  return apB->RSSI - apA->RSSI;
}

void WiFiService::scan_wifi_networks() {
  ESP_LOGI(TAG,"in scan_wifi_networks");
  wifi_scan_config_t scan_config = {
      .ssid = NULL,        // Scan all SSIDs
      .bssid = NULL,       // Scan all BSSIDs
      .channel = 0,        // Scan all channels
      .show_hidden = true, // Include hidden networks
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time = {.active = {.min = 150, .max = 500}},
      .home_chan_dwell_time = 100,
      .channel_bitmap=0,
      .coex_background_scan =
          false // a bit more aggressive, might impact bluetooth
  };
  ESP_LOGI(TAG, "trying now start scan:++++++++++++++++++++++");
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true)); // true = blocking
  uint16_t number = 0; //all channels
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&number));

  wifi_ap_record_t ap_records[number];
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_records));
  ESP_LOGI(TAG, "CALLING updateDetectedAPs WITH %d APs", number);
  APCredentialManager::updateDetectedAPs(number, ap_records);
}

void WiFiService::APCredentialManager::updateDetectedAPs(
    uint16_t number, wifi_ap_record_t *ap_records) {

  if (!initialized)
    loadDefaultAPs();
  // const WiFiService::APCredential *filtered[maxTrackedSSIDs];
  int filtered_count = 0;

  for (uint16_t i = 0; i < number && filtered_count < maxTrackedSSIDs; ++i) {
    // sanitizes and chops the SSID to ED_MAX_SSID_PWD_SIZE
    char ssid_str[ED_MAX_SSID_PWD_SIZE]; // One extra byte for null terminator
    memcpy(ssid_str, ap_records[i].ssid, ED_MAX_SSID_PWD_SIZE - 1);

    ssid_str[ED_MAX_SSID_PWD_SIZE] = '\0'; // Ensure null-termination
    ESP_LOGI(TAG, "processing {%s} rssi %d", ssid_str, ap_records[i].rssi);
    for (int j = 0; j < WiFiService::APCredentialManager::maxTrackedSSIDs;
         ++j) {
      const char *curTrackedSSID = WiFiService::APCredentialManager::getSSID(j);
      if (curTrackedSSID == nullptr)
        break;
      if (strncmp(ssid_str, curTrackedSSID, sizeof(ssid_str)) == 0) {
        activeSSIDs[filtered_count++] =
            WiFiService::APCredentialManager::findAndUpdateInfo(
                ssid_str, ap_records[i].rssi, ap_records[i].primary, j);
        ESP_LOGI(TAG, "updateDetectedAPs matches %s with RSSI %d", ssid_str,
                 ap_records[i].rssi);
        break;
      }
    }
  }
  activeSSIDs[filtered_count] = nullptr; // terminates the list with nullpt
  // ESP_LOGI(TAG,"updateDetectedAPs matches %d APs",count);
  qsort(activeSSIDs, filtered_count, sizeof(APCredential),
        APCredential::compare_rssi_desc);
}

std::vector<std::function<void()>> WiFiService::ipReadyCallbacks;

void WiFiService::subscribeToIPReady(std::function<void()> callback) {
  ESP_LOGI(TAG, " in subscribeToIPReady: subscribing");
  ipReadyCallbacks.push_back(callback);
  ESP_LOGI(TAG,
           " in subscribeToIPReady: subscribed ipReadyCallbacks size is %d",
           ipReadyCallbacks.size());
}

void WiFiService::runGotIPsubscribers() {
  ESP_LOGI(TAG, " in runGotIPsubscribers");
  uint8_t i = 1;
  for (auto &cb : ipReadyCallbacks) {
    ESP_LOGI(TAG, " runGotIPsubscribers running subscriber %d", i++);
    cb(); // Notify all subscribers
  }
}

bool WiFiService::APCredentialManager::setNextActiveAP() {
  if (!initialized)
    loadDefaultAPs();
  static int8_t curpos = 0;
  if (activeSSIDs[curpos] == nullptr) {
    curAP = nullptr;
    if (curpos == 0) {
      ESP_LOGI(TAG, "setNextActiveAP set to nullptr- no network available!");
      return false; // no reachable AP with known valid credentials
    } else
      ESP_LOGI(TAG,
               "setNextActiveAP set to nullptr, no other AP to try (was %d of "
               "%d available)",
               curpos + 1, count);
    curpos = 0; // resets to the first position
    return true;
  }
  curAP = activeSSIDs[curpos++];
  ESP_LOGI(TAG, "curAP set to %s done, index %d of %d", curAP->ssid, curpos,
           count);
  return true;
}
esp_err_t WiFiService::wifi_conn_STA() {
  ESP_LOGI(TAG, "Initializing WiFi in mode: STA, curAP is %s null ",
           APCredentialManager::curAP == nullptr ? "" : "not");

  wifi_config_t sta_config = {};
  strcpy((char *)sta_config.sta.ssid, APCredentialManager::curAP->ssid);
  strcpy((char *)sta_config.sta.password, APCredentialManager::curAP->password);
#ifdef DEBUG_BUILD
  sta_config.sta.failure_retry_cnt =
      20; //< Number of connection retries station will do before moving to next
          // AP. scan_method should be set as WIFI_ALL_CHANNEL_SCAN to use this
          // config.

#else
  sta_config.sta.failure_retry_cnt =
      15; //< Number of connection retries station will do before moving to next
          // AP. scan_method should be set as WIFI_ALL_CHANNEL_SCAN to use this
          // config.
#endif

  RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
  RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG,
                  "set config failed");
  RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
  // does not call esp_wifi_connect as it relies in the event handler to do that
  // in response to a START event

  ESP_LOGI(TAG, "WiFi started in mode: %s", "STA");
  return ESP_OK;
}

esp_err_t WiFiService::launch()

{

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init(); // NOTE it is ERASING flash if it fails. backup
                            // needs to be properly ensured
  }
  ESP_ERROR_CHECK(ret);
  // initializes internal components. Note. being a singleton you canNOT use the
  // constructor
  init_sta_retry_timer(); // the timer for retries to connect back to STA mode
                          // aftern network outages and switch to AP mode
                          // initiaizes the event loop
                          // 🔧 Add this line here
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  //  initializes TCP/IP stack
  RETURN_ON_ERROR(esp_netif_init(), TAG, "netif launch failed");

  // registers the even handlers
  RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL),
                  TAG, "event reg failed");
  RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL),
                  TAG, "IP event reg failed");
  // creates network interfaces
  sta_netif = esp_netif_create_default_wifi_sta();
  // Set the hostname on the STA network interface

  esp_netif_create_default_wifi_ap();
  // initializes Wsifi driver
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi launch failed");

  setHostName();
  // scans the actual available APs and matches against stored credentials of
  // known connectable networks
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Waiting STA mode to complete start... for station %s",
           station_ID);

  // if there are reachable known networks,
  // set as current the one with strongest signal otherwise start wifi as AP
  /*
  if (APCredentialManager::setNextActiveAP())
      return WiFiService::wifi_conn_STA();
  else
      return WiFiService::wifi_conn_AP();
      */
  return ESP_OK;
}

// void WiFiService::setHostname()
// {
//     if (sta_netif != NULL)
//     {
//         ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif,
//         WiFiService::station_ID)); ESP_LOGI(TAG, "Hostname set to: %s",
//         WiFiService::station_ID);
//     }
// }

std::optional<WiFiService::APCredential::APType>
WiFiService::APCredential::toAPType(char value) {
  switch (value) {
  case 'U':
    return APType::AP_UNCONNECTABLE;
  case 'u':
    return APType::AP_UNCONNECTABLE;
  case '0':
    return APType::AP_UNCONNECTABLE;
  default:
    return APType::AP_CONNECTABLE;
  }
};

void WiFiService::WebInterfaace::init() {
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  esp_err_t wse = httpd_start(&server, &config);
  ESP_LOGE(TAG, "httpd_start returns: %d", wse);

  httpd_uri_t root_uri = {.uri = "/",
                          .method = HTTP_GET,
                          .handler =
                              WiFiService::WebInterfaace::root_get_handler,
                          .user_ctx = NULL};

  httpd_uri_t set_ap_uri = {.uri = "/set_ap",
                            .method = HTTP_POST,
                            .handler =
                                WiFiService::WebInterfaace::set_ap_post_handler,
                            .user_ctx = NULL};
  if (wse == ESP_OK) {
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &set_ap_uri);
  }
}
esp_err_t WiFiService::WebInterfaace::root_get_handler(httpd_req_t *req) {

  /*<html><body>
  <form action="/set_ap" method="POST">
SSID: <input type="text" name="ssid"><br>
Password: <input type="password" name="password"><br>
<input type="submit" value="Update">
</form>
</body></html>

  */
  const char *html = "<html><body><form action=\"/set_ap\" method=\"POST\">"
                     "SSID: <input name=\"ssid\"><br>"
                     "Password: <input name=\"password\" type=\"password\"><br>"
                     "<input type=\"submit\" value=\"Update\">"
                     "</form></body></html>";
  httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t WiFiService::WebInterfaace::set_ap_post_handler(httpd_req_t *req) {
  // Buffer to hold incoming POST data
  char buf[100];
  int ret = httpd_req_recv(req, buf, sizeof(buf));
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  buf[ret] = '\0'; // Null-terminate the received data

  // Extract SSID and password from form data
  char ssid[ED_MAX_SSID_PWD_SIZE] = {0};
  char password[ED_MAX_SSID_PWD_SIZE] = {0};

  // Simple parsing (assumes format: ssid=MySSID&password=MyPass)
  sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password);

  ESP_LOGI("AP_CONFIG", "Received SSID: %s", ssid);
  ESP_LOGI("AP_CONFIG", "Received Password: %s", password);

  APCredentialManager::addOrUpdate(ssid, password, true);
  // Save to NVS
  // in case the credentials do not exists, they are saved as new AT tracked
  // in case the credential exists, they are saved as well to override the
  // firmware stored password
  APCredentialManager::addOrUpdateToNVS(ssid, password);
  // Respond to client
  httpd_resp_send(req, "AP settings updated successfully!",
                  HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t WiFiService::WebInterfaace::httpd_resp_send_500(httpd_req_t *req) {
  const char *error_msg = "500 Internal Server Error";
  httpd_resp_set_status(req, "500 Internal Server Error");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, error_msg, strlen(error_msg));
};

bool WiFiService::APCredentialManager::addOrUpdate(const char *ssid,
                                                   const char *password,
                                                   bool canConnect) {
  for (size_t i = 0; i < count; ++i) {
    if (credentials[i].matches(ssid)) {
      strncpy(credentials[i].password, password,
              sizeof(credentials[i].password) - 1);
      credentials[i].password[sizeof(credentials[i].password) - 1] = '\0';

      credentials[i].type = canConnect ? APCredential::APType::AP_CONNECTABLE
                                       : APCredential::APType::AP_UNCONNECTABLE;

      return true; // Updated
    }
  }

  if (count >= maxTrackedSSIDs)
    return false; // No space

  credentials[count++] = APCredential(ssid, password, canConnect);
  return true; // Added
}
/*
    bool WiFiService::APCredentialManager::packCredential( //TODO manage also
   the parameter of connection enabled. want to be able to configure and save
   monitored networks as well CodecOperation mode, std::string& ssid,
   std::string& password, std::string& codecStr)
    {
        constexpr char SEP = '\x1F';

        if (mode == CodecOperation::Join)
        {
            // Validate inputs
            if (ssid.find(SEP) != std::string::npos ||
                password.find(SEP) != std::string::npos)
                return false;
            codecStr = ssid + SEP + password;
            return true;
        }
        else
        {
            // Split result into ssid and password
            size_t sep = codecStr.find(SEP);
            if (sep == std::string::npos || sep == 0
             //   || sep == codecStr.size() - 1 allows empty password in case of
   ssid which just requires monitoring
            )
                return false;
            ssid     = codecStr.substr(0, sep);
            password = codecStr.substr(sep + 1);
            return true;
        }
    }
*/
esp_err_t
WiFiService::APCredentialManager::addOrUpdateToNVS(const char *ssid,
                                                   const char *password) {
  ED_NVS::NVSdataUnit wifiCred(NVS_AREA_NAME.data, ssid, password);
  return ED_NVS::NVSstorage::writeData(wifiCred);

  /*
          // Initialize NVS
          esp_err_t err = nvs_flash_init();
          if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
              err == ESP_ERR_NVS_NEW_VERSION_FOUND)
          {
              ESP_RETURN_ON_ERROR(err, TAG,
                                  "NVS error: %s. Manual recovery may be
     needed.", esp_err_to_name(err));
              // Optionally: enter safe mode or notify user
          }

          ESP_ERROR_CHECK(err);

          // Open NVS handle in read-write mode
          nvs_handle_t nvs_handle;
          err = nvs_open(NVS_STORAGE_KEY, NVS_READWRITE, &nvs_handle);
          if (err != ESP_OK)
              return err;

          unsigned char i = 0;
          char          ssid_key[9]; //, pass_key[9];
          bool          SSIDalreadyInNVS = false;
          // scans the stored SSID credential to try and locat a matching one
          while (true)
          {
              snprintf(ssid_key, sizeof(ssid_key), "ssid_%u", i);
              // snprintf(pass_key, sizeof(pass_key), "spwd_%u", i);

              char   nvs_ssid[ED_MAX_SSID_PWD_SIZE];
              size_t ssid_len = sizeof(nvs_ssid);

              if (nvs_get_str(nvs_handle, ssid_key, nvs_ssid, &ssid_len) !=
                      ESP_OK ||               // reached end of storage and no
     matching SSID found strcmp(nvs_ssid, ssid) == 0 // matching SSID
              )
                  break; //  i set to the new entry index in case it needs
     adding or
                         //  matching

              // Write SSID and password. if SSID already exists, it will be
     overwritten
              // safely as per documentation
              err = nvs_set_str(nvs_handle, ssid_key, ssid);
              ESP_RETURN_ON_ERROR(err, TAG,
                                  "Could not save SSID to NVS for key %s, SSID
     {%s}", ssid_key, ssid);

              err = nvs_set_str(nvs_handle, pass_key, password);
              ESP_RETURN_ON_ERROR(err, TAG,
                                  "Could not save password to NVS for key %s,
     SSID {%s}", ssid_key, ssid);

              // Commit changes
              err = nvs_commit(nvs_handle);
              ESP_RETURN_ON_ERROR(err, TAG, "Could not save commit SSID changes
     to NVS");

              // Close handle
              nvs_close(nvs_handle);
              return ESP_OK;

              */
};

esp_err_t WiFiService::APCredentialManager::loadFromNVS() {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(NVS_STORAGE_KEY, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) { // logging as warning as this is a normal situation
                       // after flashing firmware
    ESP_LOGW(TAG, "No wifi_creds available in the NVS to load");
    return err;
  }
  unsigned char i = 0;
  char ssid_key[9], pass_key[9];
  while (true) {
    snprintf(ssid_key, sizeof(ssid_key), "ssid_%u", i);
    snprintf(pass_key, sizeof(pass_key), "spwd_%u", i);

    char nvs_ssid[ED_MAX_SSID_PWD_SIZE], nvs_password[ED_MAX_SSID_PWD_SIZE];
    size_t ssid_len = sizeof(nvs_ssid), pass_len = sizeof(nvs_password);

    if (nvs_get_str(nvs_handle, ssid_key, nvs_ssid, &ssid_len) != ESP_OK ||
        nvs_get_str(nvs_handle, pass_key, nvs_password, &pass_len) != ESP_OK) {
      break; // No more entries
    }

    APCredentialManager::addOrUpdate(nvs_ssid, nvs_password, true);

    ++i;
  }

  nvs_close(nvs_handle);
  return ESP_OK;
}

} // namespace ED_wifi