/*
  Open CW Keyer MK2 - minimale Firmware 
  DL1MKP  |   11-2026   |  art-of-electronics.blog
  Arduino Nano (ATmega328P)

  Funktionen:
    - Iambic-Keyer (Mode B): linkes Paddle = Punkte, rechtes Paddle = Striche
    - Tempo wird mit dem Poti (A0)  eingestellt
    - Zwei Speicher (EEPROM), Aufnahme per Paddle, Abspielen per Tastendruck

  Bedienung:
    S2 / S3 kurz druecken  -> Speicher 1 / 2 senden (nochmal Taste oder Paddle = Abbruch)
    SET                    -> Quittungston, danach mit den Paddles  P 1  oder  P 2  morsen
                              (mit Mithoerton, ohne zu senden); Aufnahme beginnt mit langem Ton
    SET (waehrend Aufnahme)-> Aufnahme beenden und speichern
    SET vor "P 1"/"P 2" oder 10 s Pause -> Abbruch
    SET, dann X      -> Tastenverbindung zum TX ist AUS: nur noch Buzzer (auch beim Abhoeren der Speicher)
    SET, dann O      -> Tastenverbindung zum TX ist  EIN

  Rueckmeldung per Buzzer:
    1 kurzer Ton (880 Hz)  : SET erkannt, jetzt  P 1  oder  P 2  morsen
    1 langer Ton (1000 Hz) : P 1 / P 2 erkannt, Aufnahme laeuft
    2 kurze Toene          : gespeichert
    tiefer Ton (300 Hz)    : Abbruch / Speicher leer / nichts aufgenommen
    2 tiefe Toene (400 Hz) : Sendeleitung jetzt AUS      2 hohe Toene (1000 Hz): Sendeleitung jetzt EIN
    Bei ausgeschalteter TX-Leitung: Mithoerton tiefer (500 Hz), SET quittiert mit 2 Toenen

  Gespeichert wird tempounabhaengig (Punkt, Strich, Buchstaben- und Wortpause),
  abgespielt wird mit dem Tempo, das am Poti eingestellt ist.
*/

#include <EEPROM.h>

// ------------ Hardware (laut Schaltplan Open CW Keyer MK2) ----------------
const uint8_t PIN_DIT  = 2;    // linkes Paddle  = Punkte
const uint8_t PIN_DAH  = 6;    // rechtes Paddle = Striche
const uint8_t PIN_TONE = 4;    // Buzzer
const uint8_t PIN_KEY  = 11;   // Tastausgang (Optokoppler), HIGH = Taste gedrueckt
const uint8_t PIN_PTT  = 12;   // PTT-Ausgang (zweiter Optokoppler, wie im K3NG-Profil)
const uint8_t PIN_POT  = A0;   // Speed-Poti
const uint8_t PIN_BTN  = A1;   // Taster-Leiter: SET / Speicher 1 / Speicher 2

// ---------------- Einstellungen ----------------
const uint8_t  WPM_MIN = 10;               // Poti links
const uint8_t  WPM_MAX = 35;               // Poti rechts
const uint16_t TONE_HZ = 600;              // Mithoerton
const uint16_t TONE_HZ_MUTED = 500;        // Mithoerton bei ausgeschalteter Sendeleitung
const bool     PTT_ENABLE = true;          // false = PTT-Ausgang nie schalten
const unsigned long PTT_MIN_HANG_MS = 150; // PTT bleibt nach der letzten Taste mind. so lange an (oder 8 Punktlaengen)
const uint16_t MEM_MAX = 400;              // Platz pro Speicher (Zeichen-Tokens)
const unsigned long PROG_TIMEOUT_MS = 10000;

// ---------------- Speicherformat ----------------
// EEPROM: Adresse 0-1 Laenge Speicher 1, 2-3 Laenge Speicher 2, ab 4 die Daten
const uint8_t T_DIT = 1, T_DAH = 2, T_LET = 3, T_WORD = 4;
// T_LET  = zusaetzliche Pause bis Buchstabenabstand (2 Punktlaengen)
// T_WORD = zusaetzliche Pause bis Wortabstand (6 Punktlaengen)

// ---------------- Zustand ----------------
enum Mode : uint8_t { M_NORMAL, M_PROG_WAIT, M_RECORD };
Mode mode = M_NORMAL;

uint8_t       wpm;
unsigned long ditMs;
int           potRef;
bool          txUser  = true;       // Sendeleitung per Morse-Kommando X / O
bool          txBlock = false;      // vorlaeufig gesperrt (Eingabe, Aufnahme)
bool          keyHigh = false;
bool          pttOn   = false;
unsigned long pttT    = 0;

// Typ fuer die Paddle-Entprellung 
struct Deb { bool raw; bool state; unsigned long t; };

// ---------------- Taste / Mithoerton ----------------
bool txActive() { return txUser && !txBlock; }

void setKey(bool on) {
  bool tx = on && txActive();
  digitalWrite(PIN_KEY, tx ? HIGH : LOW);
  if (PTT_ENABLE && tx) { digitalWrite(PIN_PTT, HIGH); pttOn = true; }
  keyHigh = tx;
  if (!tx) pttT = millis();
  if (on) tone(PIN_TONE, txUser ? TONE_HZ : TONE_HZ_MUTED); else noTone(PIN_TONE);
}

void pttService() {          // PTT nach Ablauf der Haltezeit wieder abschalten
  if (!pttOn || keyHigh) return;
  unsigned long hang = 8 * ditMs;
  if (hang < PTT_MIN_HANG_MS) hang = PTT_MIN_HANG_MS;
  if (millis() - pttT >= hang) { digitalWrite(PIN_PTT, LOW); pttOn = false; }
}

void beep(uint16_t freq, uint16_t ms) {
  tone(PIN_TONE, freq);
  delay(ms);
  noTone(PIN_TONE);
}

// ---------------- Paddles (entprellt) ----------------
Deb dDit = {false, false, 0}, dDah = {false, false, 0};
bool ditP = false, dahP = false;

bool debounce(Deb &d, bool r) {
  unsigned long now = millis();
  if (r != d.raw) { d.raw = r; d.t = now; }
  else if (r != d.state && (now - d.t) >= 3) d.state = r;
  return d.state;
}

void readPaddles() {
  ditP = debounce(dDit, digitalRead(PIN_DIT) == LOW);
  dahP = debounce(dDah, digitalRead(PIN_DAH) == LOW);
}

// ---------------- Poti ----------------
void applyPot(int raw) {
  wpm = WPM_MIN + (uint8_t)((long)raw * (WPM_MAX - WPM_MIN) / 1023);
  ditMs = 1200UL / wpm;
}

int readPot() {
  analogRead(PIN_POT);                       // Blindmessung nach Kanalwechsel
  int sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(PIN_POT);
  return sum / 4;
}

void potService() {
  static unsigned long t = 0;
  unsigned long now = millis();
  if (now - t < 40) return;
  t = now;
  int raw = readPot();
  if (abs(raw - potRef) > 10) {              // Hysterese gegen Rauschen
    potRef = raw;
    applyPot(raw);
  }
}

// ---------------- Taster (SET / Speicher 1 / Speicher 2) ----------------
// Ladder: nichts ~1023, SET ~0, S2 ~93, S3 ~170
int8_t classifyBtn(int raw) {
  if (raw <= 46)  return 1;   // SET
  if (raw <= 131) return 2;   // Speicher 1
  if (raw <= 203) return 3;   // Speicher 2
  if (raw >= 800) return 0;   // nichts gedrueckt
  return -1;                  // Uebergang, ignorieren
}

// liefert 0 = nichts, 1 = SET, 2 = Speicher 1, 3 = Speicher 2 (einmal pro Druck)
uint8_t buttonService() {
  static unsigned long tRead = 0, tc = 0;
  static int8_t cand = 0, stable = 0;
  unsigned long now = millis();
  if (now - tRead < 5) return 0;
  tRead = now;
  analogRead(PIN_BTN);
  int8_t c = classifyBtn(analogRead(PIN_BTN));
  if (c < 0) return 0;
  if (c != cand) { cand = c; tc = now; }
  else if (cand != stable && (now - tc) >= 30) {
    stable = cand;
    if (stable != 0) return stable;
  }
  return 0;
}

// ---------------- Aufnahme ----------------
uint8_t  recBuf[MEM_MAX];
uint16_t recCount = 0;
uint8_t  recSlot = 0;
bool     recFull = false;

void recPush(uint8_t t) {
  if (recCount >= MEM_MAX) { recFull = true; return; }
  recBuf[recCount++] = t;
}

// ---------------- Eingabe "P 1" / "P 2" nach SET ----------------
uint32_t pcCode = 0;        // Elemente des aktuellen Zeichens: 1 = Punkt, 2 = Strich (z. B. P = 1221)
uint8_t  pcCount = 0;
uint8_t  progStage = 0;     // 0 = erwarte P, 1 = erwarte 1 oder 2
unsigned long progT = 0;

void progElement(bool dah) {
  if (pcCount < 6) pcCode = pcCode * 10 + (dah ? 2 : 1);
  if (pcCount < 255) pcCount++;
  progT = millis();
}

// ---------------- Iambic-Keyer (Mode B) ----------------
enum KState : uint8_t { K_IDLE, K_ELEM, K_GAP };
KState        ks = K_IDLE;
bool          kLastDah = false, ditMem = false, dahMem = false;
unsigned long kT = 0, kLen = 0, kGapLen = 0, keyUpTime = 0;

void keyerReset() {
  ks = K_IDLE;
  ditMem = dahMem = false;
  setKey(false);
  keyUpTime = millis();
}

void startElement(bool dah) {
  unsigned long now = millis();
  if (mode == M_RECORD) {
    if (recCount > 0) {
      unsigned long gap = now - keyUpTime;
      if (gap >= 5 * ditMs)      recPush(T_WORD);
      else if (gap >= 2 * ditMs) recPush(T_LET);
    }
    recPush(dah ? T_DAH : T_DIT);
  } else if (mode == M_PROG_WAIT) {
    progElement(dah);
  }
  kLastDah = dah;
  kLen = (dah ? 3 : 1) * ditMs;
  kT = now;
  ditMem = dahMem = false;
  ks = K_ELEM;
  setKey(true);
}

void keyerService() {
  unsigned long now = millis();
  switch (ks) {
    case K_IDLE:
      if (ditP || dahP) startElement(dahP && !ditP);
      break;

    case K_ELEM:
      if (ditP) ditMem = true;
      if (dahP) dahMem = true;
      if (now - kT >= kLen) {
        setKey(false);
        keyUpTime = now;
        kGapLen = ditMs;
        kT = now;
        ks = K_GAP;
      }
      break;

    case K_GAP:
      if (ditP) ditMem = true;
      if (dahP) dahMem = true;
      if (now - kT >= kGapLen) {
        bool next = false, d = false;
        if (kLastDah) {
          if (ditMem)    { next = true; d = false; }
          else if (dahP) { next = true; d = true; }
        } else {
          if (dahMem)    { next = true; d = true; }
          else if (ditP) { next = true; d = false; }
        }
        if (next) startElement(d); else ks = K_IDLE;
      }
      break;
  }
}

// ---------------- Wiedergabe ----------------
bool          playing = false;
uint8_t       pSlot = 0, pState = 0;
uint16_t      pIdx = 0, pLen = 0;
unsigned long pT = 0, pDur = 0;
const uint8_t P_KEY = 0, P_GAP = 1;

void stopPlay() {
  playing = false;
  setKey(false);
}

void playNext() {
  unsigned long now = millis();
  while (pIdx < pLen) {
    uint8_t t = EEPROM.read(4 + pSlot * MEM_MAX + pIdx);
    pIdx++;
    if (t == T_DIT || t == T_DAH) {
      pDur = (t == T_DAH ? 3 : 1) * ditMs;
      pState = P_KEY; pT = now;
      setKey(true);
      return;
    }
    if (t == T_LET)  { pDur = 2 * ditMs; pState = P_GAP; pT = now; return; }
    if (t == T_WORD) { pDur = 6 * ditMs; pState = P_GAP; pT = now; return; }
  }
  stopPlay();
}

void playService() {
  unsigned long now = millis();
  if (now - pT < pDur) return;
  if (pState == P_KEY) {
    setKey(false);
    pState = P_GAP; pT = now; pDur = ditMs;
  } else {
    playNext();
  }
}

void startPlay(uint8_t slot) {
  uint16_t len = 0;
  EEPROM.get(slot * 2, len);
  if (len == 0 || len > MEM_MAX) { beep(300, 300); return; }
  keyerReset();
  pSlot = slot; pLen = len; pIdx = 0;
  playing = true;
  playNext();
}

// ---------------- Aufnahme speichern ----------------
void finishRecord() {
  keyerReset();
  txBlock = false;
  mode = M_NORMAL;
  if (recCount == 0) {
    beep(300, 300);
    return;
  }
  for (uint16_t i = 0; i < recCount; i++) {
    EEPROM.update(4 + recSlot * MEM_MAX + i, recBuf[i]);
  }
  EEPROM.put(recSlot * 2, recCount);
  if (recFull) {
    beep(300, 300);
  } else {
    beep(1000, 80); delay(80); beep(1000, 80);
  }
}

// ---------------- Arduino ----------------
void progCancel() {
  txBlock = false;
  keyerReset();
  mode = M_NORMAL;
  beep(300, 200);
}

void startRecording() {
  recCount = 0;
  recFull = false;
  beep(1000, 150);                // langer Ton: Aufnahme laeuft
  keyerReset();
  mode = M_RECORD;
}

void progEvaluate() {                  // ein Zeichen ist fertig
  bool isP    = (pcCount == 4 && pcCode == 1221);     // P
  bool isOne  = (pcCount == 5 && pcCode == 12222);    // 1
  bool isTwo  = (pcCount == 5 && pcCode == 11222);    // 2
  bool isX    = (pcCount == 4 && pcCode == 2112);     // X  
  bool isO    = (pcCount == 3 && pcCode == 222);      // O  
  pcCode = 0;
  pcCount = 0;

  if (progStage == 0 && (isX || isO)) {
    txUser = isO;                      // O = Sendeleitung ein, X = aus
    txBlock = false;
    keyerReset();
    mode = M_NORMAL;
    if (txUser) { beep(1000, 80); delay(80); beep(1000, 80); }
    else        { beep(400, 100); delay(80); beep(400, 100); }
  } else if (progStage == 0 && isP) {
    progStage = 1;                     // P erkannt, jetzt Nummer
  } else if (progStage == 1 && (isOne || isTwo)) {
    recSlot = isOne ? 0 : 1;
    startRecording();                  // Sendeleitung bleibt waehrend der Aufnahme gesperrt
  } else {                             // falsche Eingabe: nochmal von vorn
    progStage = 0;
    beep(300, 300);
    keyerReset();
    progT = millis();
  }
}

void setup() {
  pinMode(PIN_DIT, INPUT_PULLUP);
  pinMode(PIN_DAH, INPUT_PULLUP);
  pinMode(PIN_KEY, OUTPUT);
  digitalWrite(PIN_KEY, LOW);
  pinMode(PIN_PTT, OUTPUT);
  digitalWrite(PIN_PTT, LOW);
  pinMode(PIN_TONE, OUTPUT);

  potRef = readPot();
  applyPot(potRef);
  keyerReset();
  beep(700, 60);
}

void loop() {
  readPaddles();
  potService();
  pttService();
  uint8_t b = buttonService();

  if (playing) {                // jeder Tastendruck oder jedes Paddle bricht ab
    if (b || ditP || dahP) stopPlay();
    else playService();
    return;
  }

  switch (mode) {
    case M_NORMAL:
      keyerService();
      if (b == 1) {
        txBlock = true;        // Eingabe nur mit Mithoerton, nicht senden
        mode = M_PROG_WAIT;
        pcCode = 0; pcCount = 0; progStage = 0;
        beep(880, 80);
        if (!txUser) { delay(80); beep(880, 80); } // 2 Toene = TX off
        keyerReset();
        progT = millis();
      } else if (b == 2) {
        startPlay(0);
      } else if (b == 3) {
        startPlay(1);
      }
      break;

    case M_PROG_WAIT:
      keyerService();
      if (b == 1) {
        progCancel();
      } else if (pcCount > 0 && ks == K_IDLE && (millis() - keyUpTime) >= 2 * ditMs) {
        progEvaluate();                // Zeichenende erkannt
      } else if ((millis() - progT) > PROG_TIMEOUT_MS) {
        progCancel();
      }
      break;

    case M_RECORD:
      keyerService();
      if (b == 1 || recFull) finishRecord();
      break;
  }
}
