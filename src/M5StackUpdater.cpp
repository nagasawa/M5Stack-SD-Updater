/*
 *
 * M5Stack SD Updater
 * Project Page: https://github.com/tobozo/M5Stack-SD-Updater
 *
 * Copyright 2018 tobozo http://github.com/tobozo
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files ("M5Stack SD Updater"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "M5StackUpdater.h"


static int M5_UI_Progress;

void SDUpdater::displayUpdateUI(String label) {
  M5.Lcd.setBrightness(100);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextFont(0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf(label.c_str());
  M5.Lcd.drawRect(110, 130, 102, 20, WHITE);
}


void SDUpdater::M5SDMenuProgress(int state, int size) {
  int percent = (state*100) / size;
  if( percent == M5_UI_Progress ) {
    return;
  }
  Serial.printf("percent = %d\n", percent);
  M5_UI_Progress = percent;
  uint16_t x      = M5.Lcd.getCursorX();
  uint16_t y      = M5.Lcd.getCursorY();
  int textfont    = M5.Lcd.textfont;
  int textsize    = M5.Lcd.textsize;
  int textcolor   = M5.Lcd.textcolor;
  int textbgcolor = M5.Lcd.textbgcolor;
  
  if (percent > 0 && percent < 101) {
    M5.Lcd.fillRect(111, 131, percent, 18, GREEN);
    M5.Lcd.fillRect(111+percent, 131, 100-percent, 18, BLACK);
  } else {
    M5.Lcd.fillRect(111, 131, 100, 18, BLACK);
  }
  M5.Lcd.setTextFont(0); // Select font 0 which is the Adafruit font
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(150, 155);
  M5.Lcd.print(String(percent) + "% "); // trailing space is important
  M5.Lcd.setCursor(x, y);
  M5.Lcd.setTextFont(textfont); // Select font 0 which is the Adafruit font
  M5.Lcd.setTextSize(textsize);
  M5.Lcd.setTextColor(textcolor, textbgcolor);
  
}


esp_image_metadata_t SDUpdater::getSketchMeta(const esp_partition_t* source_partition) {
  esp_image_metadata_t data;
  if (!source_partition) return data;
  const esp_partition_pos_t source_partition_pos  = {
     .offset = source_partition->address,
     .size = source_partition->size,
  };
  data.start_addr = source_partition_pos.offset;
  esp_image_verify(ESP_IMAGE_VERIFY, &source_partition_pos, &data);
  return data;//.image_len;
}

void SDUpdater::updateNVS() {
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  esp_image_metadata_t nusketchMeta = getSketchMeta( update_partition );
  uint32_t nuSize = nusketchMeta.image_len;
  Serial.printf("Updating menu.bin NVS size/digest after update: %d\n", nuSize);
  Preferences preferences;
  preferences.begin("sd-menu", false);
  preferences.putInt("menusize", nuSize);
  preferences.putBytes("digest", nusketchMeta.image_digest, 32);
  preferences.end(); 
}

// perform the actual update from a given stream
void SDUpdater::performUpdate(Stream &updateSource, size_t updateSize, String fileName) {
  displayUpdateUI("LOADING " + fileName);
  Update.onProgress(M5SDMenuProgress);
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      Serial.println("Written : " + String(written) + " successfully");
    } else {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    if (Update.end()) {
      Serial.println("OTA done!");
      if (Update.isFinished()) {

        if( strcmp( MENU_BIN, fileName.c_str() ) == 0 ) {
          // maintain NVS signature
          updateNVS();
        }

        Serial.println("Update successfully completed. Rebooting.");
      } else {
        Serial.println("Update not finished? Something went wrong!");
      }
    } else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
    }
  } else {
      Serial.println("Not enough space to begin OTA");
  }
}


// if NVS has info about MENU_BIN flash size and digest, try rollback()
void SDUpdater::tryRollback() {
  Preferences preferences;
  preferences.begin("sd-menu");
  uint32_t menuSize = preferences.getInt("menusize", 0);
  uint8_t image_digest[32];
  preferences.getBytes("digest", image_digest, 32);
  preferences.end();
  Serial.println("Trying rollback");
  
  if( menuSize == 0 ) {
    Serial.println("Failed to get expected menu size from NVS ram, can't check if rollback is worth a try..."); 
    return;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
  esp_image_metadata_t sketchMeta = getSketchMeta( update_partition );
  uint32_t nuSize = sketchMeta.image_len;
  
  if( nuSize != menuSize ) {
    Serial.printf("Cancelling rollback as flash sizes differ, update / current : %d / %d\n",  nuSize, menuSize );
    return;
  }
  
  Serial.println("Sizes match! Checking digest...");
  bool match = true;
  for(uint8_t i=0;i<32;i++) {
    if(image_digest[i]!=sketchMeta.image_digest[i]) {
      Serial.println("NO match for NVS digest :-(");
      match = false;
      break;
    }
  }
  if( match ) {
    if( Update.canRollBack() )  {
      Update.rollBack();
      Serial.println("Rollback done, restarting");
      ESP.restart();
    } else {
      Serial.println("Sorry, looks like Updater.h doesn't want to rollback :-(");
    }
  }


}

// check given FS for valid menu.bin and perform update if available
void SDUpdater::updateFromFS(fs::FS &fs, String fileName) {
  // try rollback first, it's faster!  
  tryRollback();
  // Thanks to Macbug for the hint, my old ears couldn't hear the buzzing :-)
  // See Macbug's excellent article on this tool:
  // https://macsbug.wordpress.com/2018/03/12/m5stack-sd-updater/
  dacWrite(25, 0); // turn speaker signal off
  // Also thanks to @Kongduino for a complementary way to turn off the speaker:
  // https://twitter.com/Kongduino/status/980466157701423104
  ledcDetachPin(25); // detach DAC
  File updateBin = fs.open(fileName);
  if (updateBin) {
    if(updateBin.isDirectory()){
      Serial.println("Error, this is not a file");
      updateBin.close();
      return;
    }
    size_t updateSize = updateBin.size();
    if (updateSize > 0) {
      Serial.println("Try to start update");
      // disable WDT it as suggested by twitter.com/@lovyan03
      disableCore0WDT();
      performUpdate(updateBin, updateSize, fileName);
      enableCore0WDT();
    } else {
       Serial.println("Error, file is empty");
    }
    updateBin.close();
  } else {
    Serial.println("Could not load binary from sd root");
  }
}
