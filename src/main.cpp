#if defined(ARDUINO_ESP8266_WEMOS_D1MINI)
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP8266HTTPClient.h>
#else
#include <WiFi.h>
#include <HTTPClient.h>
#endif

#include <ArduinoJson.h>
#include <time.h>

JsonDocument doc, filter;

struct SatelliteCatalogNumber
{
  const char *Name;
  const char *NORADID;
  const char *Color;
};
SatelliteCatalogNumber SCN[2] = {{"ISS", "25544", "2016"}, {"Tiangong", "48274", "63519"}};
const String URL[] = {"http://worldtimeapi.org/api/ip/8.8.8.8",
                      "http://ip-api.com/json/?fields=lat,lon",
                      "https://www.astroviewer.net/iss/ws/predictor.php?sat=",
                      "https://www.n2yo.com/sat/instant-tracking.php?d=1&s="};

time_t StringToDateTime(String D)
{
  struct tm DateTime = {D.substring(12, 14).toInt(), D.substring(10, 12).toInt(), D.substring(8, 10).toInt(),
                        D.substring(6, 8).toInt(), D.substring(4, 6).toInt(), D.substring(0, 4).toInt()};
  return mktime(&DateTime);
}

bool GetJson(String H)
{
  HTTPClient http;
  http.useHTTP10(true);
  WiFiClient client;
  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();
  doc.clear();
  http.begin(H[4] == 's' ? clientSecure : client, H);
  // do {
  http.GET();
  H = http.getString();
  // } while (H == "");
  http.end();
  deserializeJson(doc, H[0] == '[' ? H.substring(1, H.length() - 1) : H, DeserializationOption::Filter(filter));
  // serializeJsonPretty(doc, Serial);
  return !doc.isNull();
}

String DrawCir(uint W, const char *C, float Lat, float Lon)
{
  return "cirs " + String((Lon + 180.0) / 360.0 * 480.0, 0) + "," + String((-1.0 * Lat + 90.0) / 180.0 * 240.0, 0) + "," + String(W / 2) + "," + C + "\xFF\xFF\xFF";
}

void setup()
{
  Serial.begin(115200);
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  while (!Serial)
    yield();
  Serial.print("\xFF\xFF\xFFt0.txt=\"WiFi:WeMos/Vive La Resistance, http://192.168.4.1\"\xFF\xFF\xFF");
#if defined(ARDUINO_ESP8266_WEMOS_D1MINI)
  WiFiManager wm;
  wm.autoConnect("WeMos", "Vive La Resistance");
#else
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED);
#endif
  Serial.print("\xFF\xFF\xFFt0.txt=\"Connected to WiFi\"\xFF\xFF\xFF");
  // URL[0]
  filter["day_of_year"] = true;
  filter["utc_datetime"] = true;
  filter["datetime"] = true;
  // URL[1]
  filter["lat"] = true;
  filter["lon"] = true;
  // URL[2]
  filter["passes"][0]["begin"] = true;
  filter["passes"][0]["mag"] = true;
  // URL[3]
  filter["pos"][0]["d"] = true;
}

void loop()
{
  char NextPass[64], CurrentDateTime[20];
  float Lat, Lon;
  String S = "vis 255,0\xFF\xFF\xFFvis t0,1\xFF\xFF\xFF";
  if (GetJson(URL[0]))
  {
    // Calculate and draw Sun Position
    float Days = doc["utc_datetime"].as<String>().substring(11, 13).toFloat();
    Days += doc["utc_datetime"].as<String>().substring(14, 16).toFloat() / 60.0;
    Days /= 24.0;
    Days += doc["day_of_year"].as<float>() - 1.0;
    Lon = (PI * (1 - 2 * (Days - floor(Days)))) * 180.0 / PI;
    Lat = ((23.5 * PI / 180) * sin(PI * 2 / 365.25 * (Days - ((31 + 28.25 + 21) * 1.0)))) * 180.0 / PI;
    S += DrawCir(12, "65504", Lat, Lon) + DrawCir(8, "63488", Lat, Lon);

    // Save current time to be used to calculate next pass Time Remaining
    sprintf(CurrentDateTime, doc["datetime"].as<String>().substring(0, 4).c_str());
    strcat(CurrentDateTime, doc["datetime"].as<String>().substring(5, 7).c_str());
    strcat(CurrentDateTime, doc["datetime"].as<String>().substring(8, 10).c_str());
    strcat(CurrentDateTime, doc["datetime"].as<String>().substring(11, 13).c_str());
    strcat(CurrentDateTime, doc["datetime"].as<String>().substring(14, 16).c_str());
    strcat(CurrentDateTime, doc["datetime"].as<String>().substring(17, 19).c_str());

    // Draw my GPS coordinates
    if (GetJson(URL[1]))
    {
      Lat = doc["lat"].as<float>();
      Lon = doc["lon"].as<float>();
      S += "fill " + String(((Lon + 180.0) / 360.0 * 480.0) - 4, 0) + "," + String(((-1.0 * Lat + 90.0) / 180.0 * 240.0) - 4, 0) + ",9,9,0\xFF\xFF\xFF";
      S += DrawCir(7, "65504", Lat, Lon) + DrawCir(5, "31", Lat, Lon);

      // Next pass Time Remaining text using current time and my GPS coordinates
      S += "t0.txt=\"";
      for (SatelliteCatalogNumber scn : SCN)
      {
        if (GetJson(URL[2] + scn.NORADID + "&lon=" + Lon + "&lat=" + Lat))
          for (JsonObject pass : (JsonArray)doc["passes"].as<JsonArray>())
          {
            time_t RemTime = StringToDateTime(pass["begin"].as<String>()) - StringToDateTime(CurrentDateTime);
            if (RemTime > 0)
            {
              if (RemTime < 120)
                // Beep
                for (int i = 0; i < 500; i++)
                {
                  digitalWrite(5, digitalRead(4));
                  digitalWrite(4, !digitalRead(4));
                  delayMicroseconds(400);
                }
              sprintf(NextPass, "%s (%.1f): %02d:%02d:%02d  ",
                      scn.Name,
                      pass["mag"].as<float>(),
                      (int)(((RemTime / (24 * 60 * 60)) * 24) + (RemTime % (24 * 60 * 60)) / (60 * 60)),
                      (int)((RemTime % (60 * 60)) / 60),
                      (int)(RemTime % 60));
              S += String(NextPass);
              break;
            }
          }
      }
      S += "\"\xFF\xFF\xFF";
    }
  }

  // Draw space stations
  for (SatelliteCatalogNumber scn : SCN)
    if (GetJson(URL[3] + scn.NORADID))
    {
      sscanf(doc["pos"][0]["d"].as<const char *>(), "%f|%f", &Lat, &Lon);
      S += DrawCir(15, scn.Color, Lat, Lon);
    }
  Serial.print(S);
}
