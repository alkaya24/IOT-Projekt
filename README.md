# IOT-Projekt
Feuchtigkeitsmessung

In unserem Projekt ging es darum, ein Sensornetzwerk zur Messung und Überwachung der Bodenfeuchtigkeit aufzubauen. 
Dieses Netzwerk könnte man in einem Beet, auf einem Acker oder in anderen Anpflanzstellen verwenden. 
Dazu werden in Segmenten und Sektoren, deren Größe sich der „Wirt“ aussuchen kann, IOT-Devices in den Boden eingepflanzt, welche die Daten sammeln.

#Aufbau
Jedes der IOT-Devices ist mit einem ESP32 Mikrocontroller, einem Bodenfeuchtigkeitssensor und einer autonomen Stromquelle versorgt. Die Werte der Bodenfeuchtigkeit werden über die Sensoren gesammelt und durch den ESP32 über die LoRaWan Funknetztechnik an unseren TTN-Server versendet. Im TTN-Server ist ein Uplink-Data-Decoder geschrieben, der die empfangenen Daten von der HEX-Kodierung in eine leserliche ASCII-Kodierung umwandelt. Nach der Umwandlung werden die Daten über MQTT auf einem bestimmten Topic gepublisht. In einer lokalen InfluxDB-Datenbank ist ein Telegraf System integriert, dass das Topic der Daten abonniert hat, die Daten empfängt und in der Datenbank speichert. In Der Datenbank werden die gewünschten Daten gefiltert. Über die Visualisierungssoftware Grafana werden die Daten aus der Datenbank entnommen und in einem Dashboard visualisiert. Das Dashboard beinhaltet eine Visualisierung der Durchschnittswerte aller Sektoren und eine Verlaufstabelle der Bodenfeuchtigkeitswerte der Sektoren. 
