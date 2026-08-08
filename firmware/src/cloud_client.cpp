#include "cloud_client.h"
#include "config.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>

bool sendIngestPayload(const char* payload) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, CLOUD_INGEST_URL)) return false;
    http.addHeader("Content-Type", "application/json");
    char authHeader[96];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", CLOUD_SHARED_SECRET);
    http.addHeader("Authorization", authHeader);
    int statusCode = http.POST(String(payload));
    http.end();
    return statusCode >= 200 && statusCode < 300;
}
