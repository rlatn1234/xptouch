#ifndef _XLCD_CONNECTION
#define _XLCD_CONNECTION

#include <WiFi.h>
#include "mbedtls/base64.h"
#include <ArduinoJson.h>
#include "filesystem.h"
#include "paths.h"

bool xtouch_wifi_setup()
{
    DynamicJsonDocument wifiConfig(1024);
    bool cloud_mode = false;
    if(xtouch_filesystem_exist(SD, xtouch_paths_provisioning)){
        cloud_mode = true;
        lv_label_set_text(introScreenCaption, wifiConfig.isNull() ? LV_SYMBOL_SD_CARD "provisioning mode" : LV_SYMBOL_WARNING " Inaccurate provisioning.json and xtouch.json");
        lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_timer_handler();
        lv_task_handler();
        wifiConfig = xtouch_filesystem_readJson(SD, xtouch_paths_provisioning);
    }else if(xtouch_filesystem_exist(SD, xtouch_paths_config)){
        wifiConfig = xtouch_filesystem_readJson(SD, xtouch_paths_config);
        strcpy(xTouchConfig.xTouchAccessCode, wifiConfig["mqtt"]["accessCode"].as<const char *>());
        strcpy(xTouchConfig.xTouchSerialNumber, wifiConfig["mqtt"]["serialNumber"].as<const char *>());
        strcpy(xTouchConfig.xTouchHost, wifiConfig["mqtt"]["host"].as<const char *>());
        strcpy(xTouchConfig.xTouchPrinterModel, wifiConfig["mqtt"]["printerModel"].as<const char *>());

        /* xtouch.json に cloud 認証情報が含まれていれば Developer Mode 不要のクラウド MQTT を使う */
        String cloudAuthToken = (wifiConfig.containsKey("cloud") && wifiConfig["cloud"].containsKey("authToken"))
                                  ? wifiConfig["cloud"]["authToken"].as<String>() : String("");
        if (cloudAuthToken.length() > 0)
        {
            String region   = wifiConfig["cloud"].containsKey("region") ? wifiConfig["cloud"]["region"].as<String>() : String("US");
            String username = wifiConfig["cloud"].containsKey("username") ? wifiConfig["cloud"]["username"].as<String>() : String("");
            String email    = wifiConfig["cloud"].containsKey("email") ? wifiConfig["cloud"]["email"].as<String>() : String("");
            cloud.loadAuthTokensFromConfig(cloudAuthToken, region, username, email);
            lv_label_set_text(introScreenCaption, LV_SYMBOL_SD_CARD " Cloud mode (xtouch.json)");
        }
        else
        {
            lv_label_set_text(introScreenCaption, wifiConfig.isNull() ? LV_SYMBOL_WARNING " Inaccurate xtouch.json" : LV_SYMBOL_SD_CARD " Lan only mode");
        }
        lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_timer_handler();
        lv_task_handler();
    }else{
        lv_label_set_text(introScreenCaption, wifiConfig.isNull() ? LV_SYMBOL_SD_CARD " Missing provisioning.json and xtouch.json" : LV_SYMBOL_WARNING " Inaccurate provisioning.json and xtouch.json");
        lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_timer_handler();
        lv_task_handler();
        return false;
    }

    if (wifiConfig.isNull() || !wifiConfig.containsKey("ssid") || !wifiConfig.containsKey("pwd"))
    {
        lv_label_set_text(introScreenCaption, wifiConfig.isNull() ? LV_SYMBOL_SD_CARD " Missing provisioning.json and xtouch.json" : LV_SYMBOL_WARNING " Inaccurate provisioning.json and xtouch.json");
        lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_timer_handler();
        lv_task_handler();
        return false;
    }
    String ssidB64String = wifiConfig["ssid"].as<const char *>();
    String ssidPWDString = wifiConfig["pwd"].as<const char *>();
    delay(1000);

    int timeout = wifiConfig.containsKey("timeout") ? wifiConfig["timeout"].as<int>() : 3000;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidB64String.c_str(), ssidPWDString.c_str());
    ConsoleInfo.println(F("[xPTouch][CONNECTION] Connecting to WiFi .."));
    WiFi.onEvent(onWiFiEvent);

    lv_label_set_text(introScreenCaption, LV_SYMBOL_WIFI " Connecting");
    lv_obj_set_style_text_color(introScreenCaption, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_timer_handler();
    lv_task_handler();

    delay(timeout);
    wl_status_t status = WiFi.status();
    const char *statusText = "";
    lv_color_t statusColor = lv_color_hex(0x555555);

    bool reboot = false;
    while (status != WL_CONNECTED)
    {

        switch (status)
        {
        case WL_IDLE_STATUS:
            statusText = LV_SYMBOL_WIFI " Connecting";
            statusColor = lv_color_hex(0x555555);
            break;

        case WL_NO_SSID_AVAIL:
            statusText = LV_SYMBOL_WARNING " Bad SSID Check WiFi credentials";
            statusColor = lv_color_hex(0xff0000);
            reboot = true;
            break;

            // case WL_CONNECTION_LOST:
            //     break;

        case WL_CONNECT_FAILED:
        case WL_DISCONNECTED:
            statusText = LV_SYMBOL_WARNING " Check your WiFi credentials";
            statusColor = lv_color_hex(0xff0000);
            reboot = true;
            break;

        default:
            break;
        }

        if (statusText != "")
        {

            lv_label_set_text(introScreenCaption, statusText);
            lv_obj_set_style_text_color(introScreenCaption, statusColor, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_timer_handler();
            lv_task_handler();
            delay(32);
        }

        if (reboot)
        {
            delay(3000);
            lv_label_set_text(introScreenCaption, LV_SYMBOL_REFRESH " REBOOTING");
            lv_timer_handler();
            lv_task_handler();
            ESP.restart();
        }
        status = WiFi.status();
    }

    WiFi.setTxPower(WIFI_POWER_19_5dBm); // https://github.com/G6EJD/ESP32-8266-Adjust-WiFi-RF-Power-Output/blob/main/README.md

    /* Cloudモードのときのみ DNS を調整する（xtouch_cloud_apply_dns()参照）。
     * WiFi.dnsIP(0) は WiFi.config() 呼び出し前に取得してDHCP DNSを保存する。 */
    if (cloud_mode || cloud.loggedIn)
    {
        xtouch_dhcp_dns = WiFi.dnsIP(0);   /* DHCP DNS を保存（reconnect 後の再設定用） */
        xtouch_cloud_apply_dns();
    }

    delay(1000);
    lv_label_set_text(introScreenCaption, LV_SYMBOL_WIFI " Connected");
    lv_timer_handler();
    lv_task_handler();
    delay(1000);
    ConsoleInfo.print(F("[xPTouch][CONNECTION] Connected to the WiFi network with IP: "));
    ConsoleInfo.println(WiFi.localIP());
    ConsoleInfo.print(F("[xPTouch][CONNECTION] DNS: "));
    ConsoleInfo.println(WiFi.dnsIP());

    return true;
}

#endif