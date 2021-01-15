#define DEBUG
#define USE_DEMO
#define FASTLED_INTERNAL

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <FastLED.h>

#define SERVICE_UUID        "e7143cad-37d3-433c-a526-8c9c71f551a4"
#define CHARACTERISTIC_UUID "fabdc9ca-afdc-11e8-96f8-529269fb1459"

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define DATA_PIN    23
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define FPS         120

CRGB leds[128];

#define COOLING  55
#define SPARKING 120
// Array of temperature readings at each simulation cell
byte heat[120];
bool gReverseDirection = false;

CRGBPalette16 currentPalette;
TBlendType currentBlending;
uint8_t gCurrentPatternNumber = 0;  // Index number of which pattern is current
uint8_t gHue = 0;                   // rotating "base color" used by many of the patterns

// FlangeLights variables
int numLEDs = 120;
uint8_t numBolts = 4;
uint8_t dist = 1;
uint8_t brightness = 128;
uint8_t startIndex = 0;
bool playAll = false;
bool demoMode = false;
bool blinkDark = false;

struct BlinkCRGB
{
	int num;
	CRGB crgb;
};
BlinkCRGB blinkLEDs[128];

BLECharacteristic *pCharacteristic;
String oldValue = "";

void setup()
{
#ifdef DEBUG  
	Serial.begin(115200);
#endif  

	// Setup BLE server
	setupBLE();

	// Setup FastLED
	setupLEDs(numLEDs, true);
}

#ifdef USE_DEMO
// List of patterns to cycle through.  Each is defined as a separate function below
typedef void(*SimplePatternList[])();
SimplePatternList gPatterns = { /*fire,*/
	rainbow, rainbowWithGlitter, confetti, sinelon, juggle, bpm,
	rainbowSripe, rainbowSripeBlend, purpleAndGreen, totallyRandom,
	blackAndWhiteStriped, blackAndWhiteStripedBlend,
	clouds, party, redWhiteBlue, redWhiteBlueBlend,
};
extern CRGBPalette16 myRedWhiteBluePalette;
extern const TProgmemPalette16 myRedWhiteBluePalette_p PROGMEM;
#endif

void loop()
{
	// Check command
	String newValue = String(pCharacteristic->getValue().c_str());
	if (newValue != oldValue)
	{
		newValue.toLowerCase();

#ifdef DEBUG
		Serial.println(newValue);
#endif  
		oldValue = newValue;

		// Check received string for commands
		// setup strip=100 leds=60 flange=30 bolts=8
		if (newValue.startsWith("setup"))
		{
			uint8_t i = getParam(newValue, "bolts").toInt();
			if (i > 0) numBolts = i;
			i = getParam(newValue, "dist").toInt();
			if (i > 0) dist = i;
			i = getParam(newValue, "br").toInt();
			if (i > 0) brightness = i;
			i = getParam(newValue, "leds").toInt();
			if (i > 0) numLEDs = i;
			setupLEDs(i, false);
		}
		// bolt n=0 r=255 g=255 b=255 [bl=0]
		else if (newValue.startsWith("bolt"))
		{
			int num = getParam(newValue, "n").toInt();
			if (num < numBolts)
			{
				uint8_t r = getParam(newValue, "r").toInt();
				uint8_t g = getParam(newValue, "g").toInt();
				uint8_t b = getParam(newValue, "b").toInt();
				uint8_t doBlink = getParam(newValue, "bl").toInt();

				int ledNum = (num * dist) + 1;
				setLED(ledNum - 1, r, g, b);
				setLED(ledNum, r, g, b);
				setLED(ledNum + 1, r, g, b);

				setBlink(ledNum - 1, r, g, b, doBlink);
				setBlink(ledNum, r, g, b, doBlink);
				setBlink(ledNum + 1, r, g, b, doBlink);
			}
		}
		// led n=0 r=255 g=255 b=255 [bl=0]
		else if (newValue.startsWith("led"))
		{
			uint8_t num = getParam(newValue, "n").toInt();
			uint8_t r = getParam(newValue, "r").toInt();
			uint8_t g = getParam(newValue, "g").toInt();
			uint8_t b = getParam(newValue, "b").toInt();
			uint8_t doBlink = getParam(newValue, "bl").toInt();
			setLED(num, r, g, b);
			setBlink(num, r, g, b, doBlink);
		}
		else if (newValue.startsWith("off"))
		{
			resetLEDs();
		}
		else if (newValue.startsWith("pattern"))
		{
			resetLEDs();

			int n = 0;
			while (n < numLEDs)
			{
				leds[n].red = 255;
				n++;
				leds[n].red = 255;
				n += 3;
				leds[n].green = 255;
				n++;
				leds[n].green = 255;
				n += 3;
				leds[n].blue = 255;
				n++;
				leds[n].blue = 255;
				n += 3;
			}
			FastLED.show();
		}

#ifdef USE_DEMO
		// demo [n=0]
		else if (newValue.startsWith("demo"))
		{
			uint8_t num = getParam(newValue, "n").toInt();
			playAll = (num == 0) ? true : false;
			gCurrentPatternNumber = (num > 0 && num <= ARRAY_SIZE(gPatterns)) ? num - 1 : 0;
			demoMode = true;
		}
#endif
	}

	//////////////////////////////////////////////////////////////////////////////////////

	// Process blinking LEDS
	if (!demoMode)
	{
		EVERY_N_MILLISECONDS(500)
		{
			blinkDark = !blinkDark;
			for (int i = 0; i < ARRAY_SIZE(blinkLEDs); i++)
			{
				if (blinkLEDs[i].num >= 0)
				{
					if (blinkDark) setLED(blinkLEDs[i].num, 0, 0, 0);
					else setLED(blinkLEDs[i].num, blinkLEDs[i].crgb.red, blinkLEDs[i].crgb.green, blinkLEDs[i].crgb.blue);
					FastLED.show();
				}
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////

#ifdef USE_DEMO
	if (demoMode)
	{
		// Call the current pattern function once, updating the 'leds' array
		gPatterns[gCurrentPatternNumber]();

		if (gCurrentPatternNumber > 5)
		{
			startIndex++;
			FillLEDsFromPaletteColors(startIndex);
		}
		else if (gCurrentPatternNumber == 0) startIndex = 0;

		// send the 'leds' array out to the actual LED strip
		FastLED.show();
		// insert a delay to keep the framerate modest
		FastLED.delay(1000 / FPS);

		// slowly cycle the "base color" through the rainbow
		EVERY_N_MILLISECONDS(20)
		{
			gHue++;
		}
		// change patterns every 10 seconds
		EVERY_N_SECONDS(10)
		{
			if (playAll) nextPattern();
		}
	}
#endif    
}

void setupBLE()
{
	char deviceName[32] = {
		0 };
	sprintf(deviceName, "FlangeLights_%08X", ESP.getEfuseMac());
	BLEDevice::init(deviceName);

	BLEServer *pServer = BLEDevice::createServer();
	BLEService *pService = pServer->createService(SERVICE_UUID);
	pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

	oldValue = "OK";
	pCharacteristic->setValue(oldValue.c_str());
	pService->start();
	BLEAdvertising *pAdvertising = pServer->getAdvertising();
	pAdvertising->start();
#ifdef DEBUG  
	Serial.println("BLE server started");
#endif  
}

void resetLEDs()
{
	demoMode = false;
	gCurrentPatternNumber = 0;
	for (int i = 0; i < ARRAY_SIZE(leds); i++) leds[i] = CRGB::Black;
	for (int i = 0; i < ARRAY_SIZE(blinkLEDs); i++) blinkLEDs[i].num = -1;
	FastLED.showColor(CRGB::Black);
}

void setupLEDs(int num, bool initialSetup)
{
	if (num != numLEDs || initialSetup)
	{
		numLEDs = num;
		// Tell FastLED about the LED strip configuration
		FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, numLEDs).setCorrection(TypicalLEDStrip);
		// Set master brightness control
		FastLED.setBrightness(brightness);
		// Turn off all LEDs
		resetLEDs();
	}
}

void setLED(uint8_t num, uint8_t r, uint8_t g, uint8_t b)
{
	if (num < numLEDs)
	{
		leds[num].red = r;
		leds[num].green = g;
		leds[num].blue = b;
		FastLED.show();
	}
}

void setBlink(uint8_t num, uint8_t r, uint8_t g, uint8_t b, bool doBlink)
{
	for (int i = 0; i < ARRAY_SIZE(blinkLEDs); i++)
	{
		if (doBlink)
		{
			if (blinkLEDs[i].num == -1)
			{
				blinkLEDs[i].num = num;
				blinkLEDs[i].crgb.red = r;
				blinkLEDs[i].crgb.green = g;
				blinkLEDs[i].crgb.blue = b;
				break;
			}
		}
		else
		{
			if (blinkLEDs[i].num == num)
			{
				blinkLEDs[i].num = -1;
				break;
			}
		}
	}
}

String getParam(String data, String paramName)
{
	String result = "";
	int index = data.indexOf(paramName);
	if (index >= 0)
	{
		String param = data.substring(index + paramName.length());
		index = param.indexOf("=");
		if (index >= 0)
		{
			param = param.substring(index + 1);
			index = param.indexOf(" ");
			if (index >= 0) param.remove(index);
			result = param;
		}
	}
	return result;
}

#ifdef USE_DEMO

/////////////////////////////////////////////////////////////////////////////////////////////////////
// Demo code
/////////////////////////////////////////////////////////////////////////////////////////////////////
void nextPattern()
{
	// add one to the current pattern number, and wrap around at the end
	gCurrentPatternNumber = (gCurrentPatternNumber + 1) % ARRAY_SIZE(gPatterns);
}

void rainbow()
{
	// FastLED's built-in rainbow generator
	fill_rainbow(leds, numLEDs, gHue, 7);
}

void rainbowWithGlitter()
{
	// built-in FastLED rainbow, plus some random sparkly glitter
	rainbow();
	addGlitter(80);
}

void addGlitter(fract8 chanceOfGlitter)
{
	if (random8() < chanceOfGlitter)
	{
		leds[random16(numLEDs)] += CRGB::White;
	}
}

void confetti()
{
	// random colored speckles that blink in and fade smoothly
	fadeToBlackBy(leds, numLEDs, 10);
	int pos = random16(numLEDs);
	leds[pos] += CHSV(gHue + random8(64), 200, 255);
}

void sinelon()
{
	// a colored dot sweeping back and forth, with fading trails
	fadeToBlackBy(leds, numLEDs, 20);
	int pos = beatsin16(13, 0, numLEDs - 1);
	leds[pos] += CHSV(gHue, 255, 192);
}

void bpm()
{
	// colored stripes pulsing at a defined Beats-Per-Minute (BPM)
	uint8_t BeatsPerMinute = 62;
	CRGBPalette16 palette = PartyColors_p;
	uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
	for (int i = 0; i < numLEDs; i++)
	{
		leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
	}
}

void juggle() {
	// eight colored dots, weaving in and out of sync with each other
	fadeToBlackBy(leds, numLEDs, 20);
	byte dothue = 0;
	for (int i = 0; i < 8; i++)
	{
		leds[beatsin16(i + 7, 0, numLEDs - 1)] |= CHSV(dothue, 200, 255);
		dothue += 32;
	}
}

void rainbowSripe()
{
	currentPalette = RainbowStripeColors_p;
	currentBlending = NOBLEND;
}

void rainbowSripeBlend()
{
	currentPalette = RainbowStripeColors_p;
	currentBlending = LINEARBLEND;
}

void purpleAndGreen()
{
	CRGB purple = CHSV(HUE_PURPLE, 255, 255);
	CRGB green = CHSV(HUE_GREEN, 255, 255);
	CRGB black = CRGB::Black;

	currentPalette = CRGBPalette16(green, green, black, black,
		purple, purple, black, black,
		green, green, black, black,
		purple, purple, black, black);
	currentBlending = LINEARBLEND;
}

void totallyRandom()
{
	for (int i = 0; i < 16; i++) {
		currentPalette[i] = CHSV(random8(), 255, random8());
	}
	currentBlending = LINEARBLEND;
}

void blackAndWhiteStriped()
{
	// 'black out' all 16 palette entries...
	fill_solid(currentPalette, 16, CRGB::Black);
	// and set every fourth one to white.
	currentPalette[0] = CRGB::White;
	currentPalette[4] = CRGB::White;
	currentPalette[8] = CRGB::White;
	currentPalette[12] = CRGB::White;
	currentBlending = NOBLEND;
}

void blackAndWhiteStripedBlend()
{
	blackAndWhiteStriped();
	currentBlending = LINEARBLEND;
}

void clouds()
{
	currentPalette = CloudColors_p;
	currentBlending = LINEARBLEND;
}

void party()
{
	currentPalette = PartyColors_p;
	currentBlending = LINEARBLEND;
}

void redWhiteBlue()
{
	currentPalette = myRedWhiteBluePalette_p;
	currentBlending = NOBLEND;
}

void redWhiteBlueBlend()
{
	currentPalette = myRedWhiteBluePalette_p;
	currentBlending = LINEARBLEND;
}

void FillLEDsFromPaletteColors(uint8_t colorIndex)
{
	for (int i = 0; i < numLEDs; i++)
	{
		leds[i] = ColorFromPalette(currentPalette, colorIndex, brightness, currentBlending);
		colorIndex += 3;
	}
}

void fire()
{
	// Step 1.  Cool down every cell a little
	for (int i = 0; i < numLEDs; i++)
	{
		heat[i] = qsub8(heat[i], random8(0, ((COOLING * 10) / numLEDs) + 2));
	}

	// Step 2.  Heat from each cell drifts 'up' and diffuses a little
	for (int k = numLEDs - 1; k >= 2; k--)
	{
		heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
	}

	// Step 3.  Randomly ignite new 'sparks' of heat near the bottom
	if (random8() < SPARKING)
	{
		int y = random8(7);
		heat[y] = qadd8(heat[y], random8(160, 255));
	}

	// Step 4.  Map from heat cells to LED colors
	for (int j = 0; j < numLEDs - 1; j++)
	{
		CRGB color = HeatColor(heat[j]);
		int pixelnumber;
		if (gReverseDirection)
		{
			pixelnumber = (numLEDs - 1) - j;
		}
		else
		{
			pixelnumber = j;
		}
		leds[pixelnumber] = color;
	}
}

const TProgmemPalette16 myRedWhiteBluePalette_p PROGMEM =
{
	CRGB::Red,
	CRGB::Gray, // 'white' is too bright compared to red and blue
	CRGB::Blue,
	CRGB::Black,

	CRGB::Red,
	CRGB::Gray,
	CRGB::Blue,
	CRGB::Black,

	CRGB::Red,
	CRGB::Red,
	CRGB::Gray,
	CRGB::Gray,
	CRGB::Blue,
	CRGB::Blue,
	CRGB::Black,
	CRGB::Black
};

#endif
