#include <Arduino.h>
#include <digital_io.h>
/*
 * SCSI-HD Device Emulator for STM32F103
 */

#define X1TURBO_DTC510B      0 /* for SHARP X1turbo */
#define READ_SPEED_OPTIMIZE  1 /* Speed ​​up leads */
#define WRITE_SPEED_OPTIMIZE 1 /* Speed ​​up lights */
#define USE_DB2ID_TABLE      1 /* Use table to get ID from SEL-DB */

// SCSI config
#define NUM_SCSIID	7          // Support maximum SCSI-ID number (minimum is 0)
#define NUM_SCSILUN	2          // Maximum number of LUNs supported (minimum is 0)
#define READ_PARITY_CHECK 0    // Perform read parity check (unverified)

// HDD format
#if X1TURBO_DTC510B
#define BLOCKSIZE 256               // 1 block size
#else
#define BLOCKSIZE 512               // 1 block size
#endif

#include <SPI.h>

#include <SdFatConfig.h>
//Make SdFatEX CLASS available
#undef  ENABLE_EXTENDED_TRANSFER_CLASS
#define ENABLE_EXTENDED_TRANSFER_CLASS 1
#define IMPLEMENT_SPI_PORT_SELECTION 1
#include <SdFat.h>

#ifdef USE_STM32_DMA
#warning "warning USE_STM32_DMA"
#endif


#if 1
#define LOG(XX)     Serial.print(XX)
#define LOGHEX(XX)  Serial.print(XX, HEX)
#define LOGN(XX)    Serial.println(XX)
#define LOGHEXN(XX) Serial.println(XX, HEX)
#else
#define LOG(XX)     //Serial.print(XX)
#define LOGHEX(XX)  //Serial.print(XX, HEX)
#define LOGN(XX)    //Serial.println(XX)
#define LOGHEXN(XX) //Serial.println(XX, HEX)
#endif

#define active   1
#define inactive 0
#define high 0
#define low 1

#define isHigh(XX) ((XX) == high)
#define isLow(XX) ((XX) != high)

#if 0
#define gpio_mode(pin,val) gpio_set_mode(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit, val);
#define gpio_write(pin,val) gpio_write_bit(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit, val)
#define gpio_read(pin) gpio_read_bit(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit)
#else
#define GPIO_OUTPUT_OD  LL_GPIO_OUTPUT_OPENDRAIN
#define GPIO_INPUT_PU   LL_GPIO_MODE_INPUT
#define gpio_mode(pin,val) LL_GPIO_SetPinOutputType(pin, val)
#define gpio_write(pin,val) digital_io_write(pin, val)
#define gpio_read(pin) digital_io_read(pin)
#endif

//#define DB0       PB8     // SCSI:DB0
//#define DB1       PB9     // SCSI:DB1
//#define DB2       PB10    // SCSI:DB2
//#define DB3       PB11    // SCSI:DB3
//#define DB4       PB12    // SCSI:DB4
//#define DB5       PB13    // SCSI:DB5
//#define DB6       PB14    // SCSI:DB6
//#define DB7       PB15    // SCSI:DB7
//#define DBP       PB0     // SCSI:DBP
#if 1
#define ATN       GPIOA,8      // SCSI:ATN
#define BSY       GPIOA,9      // SCSI:BSY
#define ACK       GPIOA,10     // SCSI:ACK
#define RST       GPIOA,15     // SCSI:RST
#define MSG       GPIOB,3      // SCSI:MSG
#define SEL       GPIOB,4      // SCSI:SEL
#define CD        GPIOB,5      // SCSI:C/D
#define REQ       GPIOB,6      // SCSI:REQ
#define IO        GPIOB,7      // SCSI:I/O

#define SD_CS     GPIOA,4      // SDCARD:CS
#define LED       GPIOC,13     // LED
#else
#define ATN       PA8      // SCSI:ATN
#define BSY       PA9      // SCSI:BSY
#define ACK       PA10     // SCSI:ACK
#define RST       PA15     // SCSI:RST
#define MSG       PB3      // SCSI:MSG
#define SEL       PB4      // SCSI:SEL
#define CD        PB5      // SCSI:C/D
#define REQ       PB6      // SCSI:REQ
#define IO        PB7      // SCSI:I/O

#define SD_CS     PA4      // SDCARD:CS
#define LED       PC13     // LED
#endif

// GPIO register port
#define PAREG GPIOA
#define PBREG GPIOB

// LED control
#define LED_ON()       gpio_write(LED, high);
#define LED_OFF()      gpio_write(LED, low);


// Virtual pin (Arduio compatibility is slow, so make it MCU dependent)
#define PA(BIT)       (BIT)
#define PB(BIT)       (BIT+16)
// Virtual pin decoding
#define GPIOREG(VPIN)    ((VPIN)>=16?PBREG:PAREG)
#define BITMASK(VPIN) (1<<((VPIN)&15))

#define vATN       PA(8)      // SCSI:ATN
#define vBSY       PA(9)      // SCSI:BSY
#define vACK       PA(10)     // SCSI:ACK
#define vRST       PA(15)     // SCSI:RST
#define vMSG       PB(3)      // SCSI:MSG
#define vSEL       PB(4)      // SCSI:SEL
#define vCD        PB(5)      // SCSI:C/D
#define vREQ       PB(6)      // SCSI:REQ
#define vIO        PB(7)      // SCSI:I/O
#define vSD_CS     PA(4)      // SDCARD:CS

// SCSI output pin control: opendrain active LOW (direct pin drive)
#define SCSI_OUT(VPIN,ACTIVE) { GPIOREG(VPIN)->BSRR = BITMASK(VPIN)<<((ACTIVE)?16:0); }

// SCSI input pin check (inactive=0,avtive=1)
#define SCSI_IN(VPIN) ((~GPIOREG(VPIN)->IDR>>(VPIN&15))&1)

// GPIO mode
// IN , FLOAT      : 4
// IN , PU/PD      : 8
// OUT, PUSH/PULL  : 3
// OUT, OD         : 1
//#define DB_MODE_OUT 3
#define DB_MODE_OUT 1
#define DB_MODE_IN  8

// Set DB and DP to output mode
#define SCSI_DB_OUTPUT() { PBREG->CRL=(PBREG->CRL &0xfffffff0)|DB_MODE_OUT; PBREG->CRH = 0x11111111*DB_MODE_OUT; }
// Set DB and DP to input mode
#define SCSI_DB_INPUT()  { PBREG->CRL=(PBREG->CRL &0xfffffff0)|DB_MODE_IN ; PBREG->CRH = 0x11111111*DB_MODE_IN;  }

// Turn on output for BSY only
#define SCSI_BSY_ACTIVE()      { \
  gpio_mode(BSY, GPIO_OUTPUT_OD); \
  SCSI_OUT(vBSY,  active); \
}
// Turn BSY, REQ, MSG, CD, IO output ON (no change required for OD)
#define SCSI_TARGET_ACTIVE()   { }
// BSY,REQ,MSG,CD,IO output is turned off, BSY is finally input
#define SCSI_TARGET_INACTIVE() { \
  SCSI_OUT(vREQ,inactive); \
  SCSI_OUT(vMSG,inactive); \
  SCSI_OUT(vCD,inactive); \
  SCSI_OUT(vIO,inactive); \
  SCSI_OUT(vBSY,inactive); \
  gpio_mode(BSY, GPIO_INPUT_PU); \
}

// HDDiamge file
#define HDIMG_FILE "HDxx.HDS"       // HD image file name base
#define HDIMG_ID_POS  2             // Position to embed ID number
#define HDIMG_LUN_POS 3             // Position to embed LUN number

SPIClass SPI_1(0,1,2);
SdFatEX  SD(&SPI_1);

// HDD image
typedef struct hddimg_struct
{
	File          m_file;               // File object
	uint32_t      m_fileSize;           // file size
}HDDIMG;
HDDIMG	img[NUM_SCSIID][NUM_SCSILUN];   // Maximum number

uint8_t       m_senseKey = 0;         // Sense key
volatile bool m_isBusReset = false;   // Bus reset

byte          scsi_id_mask;           // Mask list of responding SCSI IDs
byte          m_id;                   // SCSI-ID currently responding
byte          m_lun;                  // Logical unit number currently responding
byte          m_sts;                  // Status byte
byte          m_msg;                  // Message byte
HDDIMG       *m_img;                  // HDD image for current SCSI-ID and LUN
byte          m_buf[BLOCKSIZE+1];     // General-purpose buffer + overrun fetch
int           m_msc;
bool          m_msb[256];

/*
 * Data byte to BSRR register set value and parity table
*/

// パリティービット生成
#define PTY(V)   (1^((V)^((V)>>1)^((V)>>2)^((V)>>3)^((V)>>4)^((V)>>5)^((V)>>6)^((V)>>7))&1)

// データバイト to BSRRレジスタ設定値変換テーブル
// BSRR[31:24] =  DB[7:0]
// BSRR[   16] =  PTY(DB)
// BSRR[15: 8] = ~DB[7:0]
// BSRR[    0] = ~PTY(DB)

// DBPのセット、REQ=inactiveにする
#define DBP(D)    ((((((uint32_t)(D)<<8)|PTY(D))*0x00010001)^0x0000ff01)|BITMASK(vREQ))

#define DBP8(D)   DBP(D),DBP(D+1),DBP(D+2),DBP(D+3),DBP(D+4),DBP(D+5),DBP(D+6),DBP(D+7)
#define DBP32(D)  DBP8(D),DBP8(D+8),DBP8(D+16),DBP8(D+24)

// DBのセット,DPのセット,REQ=H(inactrive) を同時に行うBSRRレジスタ制御値
static const uint32_t db_bsrr[256]={
  DBP32(0x00),DBP32(0x20),DBP32(0x40),DBP32(0x60),
  DBP32(0x80),DBP32(0xA0),DBP32(0xC0),DBP32(0xE0)
};
// パリティービット取得
#define PARITY(DB) (db_bsrr[DB]&1)

// マクロの掃除
#undef DBP32
#undef DBP8
//#undef DBP
//#undef PTY

#if USE_DB2ID_TABLE
/* DB to SCSI-ID テーブル */
static const byte db2scsiid[256]={
  0xff,
  0,
  1,1,
  2,2,2,2,
  3,3,3,3,3,3,3,3,
  4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
  5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};
#endif




void onFalseInit(void);
void onBusReset(void);

/*
 * IO読み込み.
 */
inline byte readIO(void)
{
  //ポート入力データレジスタ
  uint32_t ret = GPIOB->IDR;
  byte bret = (byte)((~ret)>>8);
#if READ_PARITY_CHECK
  if((db_bsrr[bret]^ret)&1)
    m_sts |= 0x01; // parity error
#endif

  return bret;
}

/*
 * 初期化.
 *  バスの初期化、PINの向きの設定を行う
 */
void setup()
{
  // PA15 / PB3 / PB4 が使えない
  // JTAG デバッグ用に使われているからです。
//  disableDebugPorts();

  //シリアル初期化
  //Serial.begin(9600);
  //while (!Serial);

  //PINの初期化
  gpio_mode(LED, GPIO_OUTPUT_OD);
  gpio_write(LED, low);

  //GPIO(SCSI BUS)初期化
  //ポート設定レジスタ（下位）
//  GPIOB->CRL |= 0x000000008; // SET INPUT W/ PUPD on PAB-PB0
  //ポート設定レジスタ（上位）
  //GPIOB->CRH = 0x88888888; // SET INPUT W/ PUPD on PB15-PB8
//  GPIOB->ODR = 0x0000FF00; // SET PULL-UPs on PB15-PB8
  // DB,DPは入力モード
  SCSI_DB_INPUT()

  // 入力ポート
  gpio_mode(ATN, GPIO_INPUT_PU);
  gpio_mode(BSY, GPIO_INPUT_PU);
  gpio_mode(ACK, GPIO_INPUT_PU);
  gpio_mode(RST, GPIO_INPUT_PU);
  gpio_mode(SEL, GPIO_INPUT_PU);
  // 出力ポート
  gpio_mode(MSG, GPIO_OUTPUT_OD);
  gpio_mode(CD,  GPIO_OUTPUT_OD);
  gpio_mode(REQ, GPIO_OUTPUT_OD);
  gpio_mode(IO,  GPIO_OUTPUT_OD);
  // 出力ポートはOFFにする
  SCSI_TARGET_INACTIVE()

#if X1TURBO_DTC510B
  // シリアル初期化
  Serial.begin(9600);
  for(int tout=3000/200;tout;tout--)
  {
    if(Serial) break;
    Serial.print(".");
    delay(200);
    break;
  }
  Serial.println("DTC510B HDD emulator for X1turbo");
#endif

  //RSTピンの状態がHIGHからLOWに変わったときに発生
  //attachInterrupt(PIN_MAP[RST].gpio_bit, onBusReset, FALLING);

  LED_ON();

  // clock = 36MHz , about 4Mbytes/sec
  // TODO: fix the mapping PA4/SD_CS
  if(!SD.begin(PA4 /*SD_CS*/,SPI_FULL_SPEED)) {
    Serial.println("SD initialization failed!");
    onFalseInit();
  }

  //セクタデータオーバーランバイトの設定
  m_buf[BLOCKSIZE] = 0xff; // DB0 all off,DBP off
  //HDイメージファイルオープン
  //int totalImage = 0;
  scsi_id_mask = 0x00;
  for(int id=0;id<NUM_SCSIID;id++)
  {
    for(int lun=0;lun<NUM_SCSILUN;lun++)
    {
      HDDIMG *h = &img[id][lun];
      char file_path[sizeof(HDIMG_FILE)+1] = HDIMG_FILE;
      // build file path
      file_path[HDIMG_ID_POS ] = '0'+id;
      file_path[HDIMG_LUN_POS] = '0'+lun;

      h->m_fileSize = 0;
      h->m_file     = SD.open(file_path, O_RDWR);
      if(h->m_file.isOpen())
      {
        h->m_fileSize = h->m_file.size();
        Serial.print("Found Imagefile:");
        Serial.print(file_path);
    		if(h->m_fileSize==0)
    		{
    			h->m_file.close();
    		}
    		else
    		{
    			Serial.print(" / ");
    			Serial.print(h->m_fileSize);
    			Serial.print("bytes / ");
    			Serial.print(h->m_fileSize / 1024);
    			Serial.print("KiB / ");
    			Serial.print(h->m_fileSize / 1024 / 1024);
    			Serial.println("MiB");
    			// 応答するIDとしてマーキング
    			scsi_id_mask |= 1<<id;
    			//totalImage++;
        }
      }
    }
  }
  // イメージファイルが０ならエラー
  if(scsi_id_mask==0) onFalseInit();
  // ドライブマップの表示
  Serial.println("I:<-----LUN----->:");
  Serial.println("D:0:1:2:3:4:5:6:7:");
  for(int id=0;id<NUM_SCSIID;id++)
  {
    Serial.print(id);
    for(int lun=0;lun<NUM_SCSILUN;lun++)
    {
      if( (lun<NUM_SCSILUN) && (img[id][lun].m_file))
        Serial.print(":*");
      else
        Serial.print(":-");
    }
    Serial.println(":");
  }
#if 0
  //test dump table
  for(int i=0;i<256;i++)
  {
    if(i%16==0)
    {
      Serial.println(' ');
      Serial.print(i, HEX);
      Serial.print(':');
    }
    Serial.print(db_bsrr[i], HEX);
    Serial.print(',');
  }
#endif
  LED_OFF();
  //RSTピンの状態がHIGHからLOWに変わったときに発生
  attachInterrupt(15, onBusReset, FALLING);
//  attachInterrupt(PIN_MAP[RST].gpio_bit, onBusReset, FALLING);
}

/*
 * 初期化失敗.
 */
void onFalseInit(void)
{
  while(true) {
    gpio_write(LED, high);
    delay(500);
    gpio_write(LED, low);
    delay(500);
  }
}

/*
 * バスリセット割り込み.
 */
void onBusReset(void)
{
#if X1TURBO_DTC510B
  // RSTパルスはライトサイクル+2クロック、1.25usくらいしかでない。
  {{
#else
  if(isHigh(gpio_read(RST))) {
    delayMicroseconds(20);
    if(isHigh(gpio_read(RST))) {
#endif
	// BUSFREEはメイン処理で行う
//      gpio_mode(MSG, GPIO_OUTPUT_OD);
//      gpio_mode(CD,  GPIO_OUTPUT_OD);
//      gpio_mode(REQ, GPIO_OUTPUT_OD);
//      gpio_mode(IO,  GPIO_OUTPUT_OD);
    	// DB,DBPは一旦入力にしたほうがいい？
    	SCSI_DB_INPUT()

      LOGN("BusReset!");
      m_isBusReset = true;
    }
  }
}

/*
 * ハンドシェイクで読み込む.
 */
inline byte readHandshake(void)
{
  SCSI_OUT(vREQ,active)
  //SCSI_DB_INPUT()
  while(!SCSI_IN(vACK)) { if(m_isBusReset) return 0; }
  byte r = readIO();
  SCSI_OUT(vREQ,inactive)
  while( SCSI_IN(vACK)) { if(m_isBusReset) return 0; }
  return r;
}

/*
 * ハンドシェイクで書込み.
 */
inline void writeHandshake(byte d)
{
  GPIOB->BSRR = db_bsrr[d]; // setup DB,DBP (160ns)
  SCSI_DB_OUTPUT() // (180ns)
  // ACK.Fall to DB output delay 100ns(MAX)  (DTC-510B)
  SCSI_OUT(vREQ,inactive) // setup wait (30ns)
  SCSI_OUT(vREQ,inactive) // setup wait (30ns)
  SCSI_OUT(vREQ,inactive) // setup wait (30ns)
  SCSI_OUT(vREQ,active)   // (30ns)
  //while(!SCSI_IN(vACK)) { if(m_isBusReset){ SCSI_DB_INPUT() return; }}
  while(!m_isBusReset && !SCSI_IN(vACK));
  // ACK.Fall to REQ.Raise delay 500ns(typ.) (DTC-510B)
  GPIOB->BSRR = DBP(0xff);  // DB=0xFF , SCSI_OUT(vREQ,inactive)
  // REQ.Raise to DB hold time 0ns
  SCSI_DB_INPUT() // (150ns)
  while( SCSI_IN(vACK)) { if(m_isBusReset) return; }
}

/*
 * データインフェーズ.
 *  データ配列 p を len バイト送信する。
 */
void writeDataPhase(int len, const byte* p)
{
  LOGN("DATAIN PHASE");
  SCSI_OUT(vMSG,inactive) //  gpio_write(MSG, low);
  SCSI_OUT(vCD ,inactive) //  gpio_write(CD, low);
  SCSI_OUT(vIO ,  active) //  gpio_write(IO, high);
  for (int i = 0; i < len; i++) {
    if(m_isBusReset) {
      return;
    }
    writeHandshake(p[i]);
  }
}

/*
 * データインフェーズ.
 *  SDカードからの読み込みながら len ブロック送信する。
 */
void writeDataPhaseSD(uint32_t adds, uint32_t len)
{
  LOGN("DATAIN PHASE(SD)");
  uint32_t pos = adds * BLOCKSIZE;
  m_img->m_file.seek(pos);

  SCSI_OUT(vMSG,inactive) //  gpio_write(MSG, low);
  SCSI_OUT(vCD ,inactive) //  gpio_write(CD, low);
  SCSI_OUT(vIO ,  active) //  gpio_write(IO, high);

  for(uint32_t i = 0; i < len; i++) {
      // 非同期リードにすれば速くなるんだけど...
    m_img->m_file.read(m_buf, BLOCKSIZE);

#if READ_SPEED_OPTIMIZE

//#define REQ_ON() SCSI_OUT(vREQ,active)
#define REQ_ON() (*db_dst = BITMASK(vREQ)<<16)
#define FETCH_SRC()   (src_byte = *srcptr++)
#define FETCH_BSRR_DB() (bsrr_val = bsrr_tbl[src_byte])
#define REQ_OFF_DB_SET(BSRR_VAL) *db_dst = BSRR_VAL
#define WAIT_ACK_ACTIVE()   while(!m_isBusReset && !SCSI_IN(vACK))
#define WAIT_ACK_INACTIVE() do{ if(m_isBusReset) return; }while(SCSI_IN(vACK))

    SCSI_DB_OUTPUT()
    register byte *srcptr= m_buf;                 // ソースバッファ
    /*register*/ byte src_byte;                       // 送信データバイト
    register const uint32_t *bsrr_tbl = db_bsrr;  // BSRRに変換するテーブル
    register uint32_t bsrr_val;                   // 出力するBSRR値(DB,DBP,REQ=ACTIVE)
    register volatile uint32_t *db_dst = &(GPIOB->BSRR); // 出力ポート

    // prefetch & 1st out
    FETCH_SRC();
    FETCH_BSRR_DB();
    REQ_OFF_DB_SET(bsrr_val);
    // DB.set to REQ.F setup 100ns max (DTC-510B)
    // ここには多少のウェイトがあったほうがいいかも
    //　WAIT_ACK_INACTIVE();
    do{
      // 0
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      // ACK.F  to REQ.R       500ns typ. (DTC-510B)
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 1
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 2
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 3
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 4
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 5
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 6
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
      // 7
      REQ_ON();
      FETCH_SRC();
      FETCH_BSRR_DB();
      WAIT_ACK_ACTIVE();
      REQ_OFF_DB_SET(bsrr_val);
      WAIT_ACK_INACTIVE();
    }while(srcptr < m_buf+BLOCKSIZE);
    SCSI_DB_INPUT()
#else
    for(int j = 0; j < BLOCKSIZE; j++) {
      if(m_isBusReset) {
        return;
      }
      writeHandshake(m_buf[j]);
    }
#endif
  }
}

/*
 * データアウトフェーズ.
 *  len ブロック読み込むこむ
 */
void readDataPhase(int len, byte* p)
{
  LOGN("DATAOUT PHASE");
  SCSI_OUT(vMSG,inactive) //  gpio_write(MSG, low);
  SCSI_OUT(vCD ,inactive) //  gpio_write(CD, low);
  SCSI_OUT(vIO ,inactive) //  gpio_write(IO, low);
  for(uint32_t i = 0; i < len; i++)
    p[i] = readHandshake();
}

/*
 * データアウトフェーズ.
 *  len ブロック読み込みながら SDカードへ書き込む。
 */
void readDataPhaseSD(uint32_t adds, uint32_t len)
{
  LOGN("DATAOUT PHASE(SD)");
  uint32_t pos = adds * BLOCKSIZE;
  m_img->m_file.seek(pos);
  SCSI_OUT(vMSG,inactive) //  gpio_write(MSG, low);
  SCSI_OUT(vCD ,inactive) //  gpio_write(CD, low);
  SCSI_OUT(vIO ,inactive) //  gpio_write(IO, low);
  for(uint32_t i = 0; i < len; i++) {
#if WRITE_SPEED_OPTIMIZE
	register byte *dstptr= m_buf;
    for(int j = 0; j < BLOCKSIZE/8; j++) {
      dstptr[0] = readHandshake();
      dstptr[1] = readHandshake();
      dstptr[2] = readHandshake();
      dstptr[3] = readHandshake();
      dstptr[4] = readHandshake();
      dstptr[5] = readHandshake();
      dstptr[6] = readHandshake();
      dstptr[7] = readHandshake();
      if(m_isBusReset) {
        return;
      }
	  dstptr+=8;
    }
#else
    for(int j = 0; j < BLOCKSIZE; j++) {
      if(m_isBusReset) {
        return;
      }
      m_buf[j] = readHandshake();
    }
#endif
    m_img->m_file.write(m_buf, BLOCKSIZE);
  }
  m_img->m_file.flush();
}

/*
 * INQUIRY コマンド処理.
 */
byte onInquiryCommand(byte len)
{
  byte buf[36] = {
    0x00, //デバイスタイプ
    0x00, //RMB = 0
    0x01, //ISO,ECMA,ANSIバージョン
    0x01, //レスポンスデータ形式
    35 - 4, //追加データ長
    0, 0, //Reserve
    0x00, //サポート機能
    'T', 'N', 'B', ' ', ' ', ' ', ' ', ' ',
    'A', 'r', 'd', 'S', 'C', 'S', 'i', 'n', 'o', ' ', ' ',' ', ' ', ' ', ' ', ' ',
    '0', '0', '1', '0',
  };
  writeDataPhase(len < 36 ? len : 36, buf);
  return 0x00;
}

/*
 * REQUEST SENSE コマンド処理.
 */
void onRequestSenseCommand(byte len)
{
  byte buf[18] = {
    0x70,   //CheckCondition
    0,      //セグメント番号
    0x00,   //センスキー
    0, 0, 0, 0,  //インフォメーション
    17 - 7 ,   //追加データ長
    0,
  };
  buf[2] = m_senseKey;
  m_senseKey = 0;
  writeDataPhase(len < 18 ? len : 18, buf);
}

/*
 * READ CAPACITY コマンド処理.
 */
byte onReadCapacityCommand(byte pmi)
{
  if(!m_img) return 0x02; // イメージファイル不在

  uint32_t bc = m_img->m_fileSize / BLOCKSIZE;
  uint32_t bl = BLOCKSIZE;
  uint8_t buf[8] = {
    bc >> 24, bc >> 16, bc >> 8, bc,
    bl >> 24, bl >> 16, bl >> 8, bl
  };
  writeDataPhase(8, buf);
  return 0x00;
}

/*
 * READ6/10 コマンド処理.
 */
byte onReadCommand(uint32_t adds, uint32_t len)
{
  LOGN("-R");
  LOGHEXN(adds);
  LOGHEXN(len);

  if(!m_img) return 0x02; // イメージファイル不在

  gpio_write(LED, high);
  writeDataPhaseSD(adds, len);
  gpio_write(LED, low);
  return 0x00; //sts
}

/*
 * WRITE6/10 コマンド処理.
 */
byte onWriteCommand(uint32_t adds, uint32_t len)
{
  LOGN("-W");
  LOGHEXN(adds);
  LOGHEXN(len);

  if(!m_img) return 0x02; // イメージファイル不在

  gpio_write(LED, high);
  readDataPhaseSD(adds, len);
  gpio_write(LED, low);
  return 0; //sts
}

/*
 * MODE SENSE コマンド処理.
 */
byte onModeSenseCommand(byte dbd, int pageCode, uint32_t len)
{
  if(!m_img) return 0x02; // イメージファイル不在

  memset(m_buf, 0, sizeof(m_buf));
  int a = 4;
  if(dbd == 0) {
    uint32_t bc = m_img->m_fileSize / BLOCKSIZE;
    uint32_t bl = BLOCKSIZE;
    byte c[8] = {
      0,//デンシティコード
      bc >> 16, bc >> 8, bc,
      0, //Reserve
      bl >> 16, bl >> 8, bl
    };
    memcpy(&m_buf[4], c, 8);
    a += 8;
    m_buf[3] = 0x08;
  }
  switch(pageCode) {
  case 0x3F:
  case 0x03:  //ドライブパラメータ
    m_buf[a + 0] = 0x03; //ページコード
    m_buf[a + 1] = 0x16; // ページ長
    m_buf[a + 11] = 0x3F;//セクタ数/トラック
    a += 24;
    if(pageCode != 0x3F) {
      break;
    }
  case 0x04:  //ドライブパラメータ
    {
      uint32_t bc = m_img->m_fileSize / BLOCKSIZE;
      m_buf[a + 0] = 0x04; //ページコード
      m_buf[a + 1] = 0x16; // ページ長
      m_buf[a + 2] = bc >> 16;// シリンダ長
      m_buf[a + 3] = bc >> 8;
      m_buf[a + 4] = bc;
      m_buf[a + 5] = 1;   //ヘッド数
      a += 24;
    }
    if(pageCode != 0x3F) {
      break;
    }
  default:
    break;
  }
  m_buf[0] = a - 1;
  writeDataPhase(len < a ? len : a, m_buf);
  return 0x00;
}


#if X1TURBO_DTC510B
/*
 * dtc510b_setDriveparameter
 */
#define PACKED  __attribute__((packed))
typedef struct PACKED dtc500_cmd_c2_param_struct
{
	uint8_t	StepPlusWidth;				// Default is 13.6usec (11)
	uint8_t	StepPeriod;					// Default is  3  msec.(60)
	uint8_t	StepMode;					// Default is  Bufferd (0)
	uint8_t	MaximumHeadAdress;			// Default is 4 heads (3)
	uint8_t	HighCylinderAddressByte;	// Default set to 0   (0)
	uint8_t	LowCylinderAddressByte;		// Default is 153 cylinders (152)
	uint8_t	ReduceWrietCurrent;			// Default is above Cylinder 128 (127)
	uint8_t	DriveType_SeekCompleteOption;// (0)
	uint8_t	Reserved8;					// (0)
	uint8_t	Reserved9;					// (0)
} DTC510_CMD_C2_PARAM;

static void logStrHex(const char *msg,uint32_t num)
{
    LOG(msg);
    LOGHEXN(num);
}

static byte dtc510b_setDriveparameter(void)
{
	DTC510_CMD_C2_PARAM DriveParameter;
	uint16_t maxCylinder;
	uint16_t numLAD;
	//uint32_t stepPulseUsec;
	int StepPeriodMsec;

	// receive paramter
	writeDataPhase(sizeof(DriveParameter),(byte *)(&DriveParameter));

	maxCylinder =
		(((uint16_t)DriveParameter.HighCylinderAddressByte)<<8) |
		(DriveParameter.LowCylinderAddressByte);
	numLAD = maxCylinder * (DriveParameter.MaximumHeadAdress+1);
	//stepPulseUsec  = calcStepPulseUsec(DriveParameter.StepPlusWidth);
	StepPeriodMsec = DriveParameter.StepPeriod*50;
	logStrHex	(" StepPlusWidth      : ",DriveParameter.StepPlusWidth);
	logStrHex	(" StepPeriod         : ",DriveParameter.StepPeriod   );
	logStrHex	(" StepMode           : ",DriveParameter.StepMode     );
	logStrHex	(" MaximumHeadAdress  : ",DriveParameter.MaximumHeadAdress);
	logStrHex	(" CylinderAddress    : ",maxCylinder);
	logStrHex	(" ReduceWrietCurrent : ",DriveParameter.ReduceWrietCurrent);
	logStrHex	(" DriveType/SeekCompleteOption : ",DriveParameter.DriveType_SeekCompleteOption);
  logStrHex (" Maximum LAD        : ",numLAD-1);
	return	0; // error result
}
#endif

/*
 * MsgIn2.
 */
void MsgIn2(int msg)
{
  LOGN("MsgIn2");
  SCSI_OUT(vMSG,  active) //  gpio_write(MSG, high);
  SCSI_OUT(vCD ,  active) //  gpio_write(CD, high);
  SCSI_OUT(vIO ,  active) //  gpio_write(IO, high);
  writeHandshake(msg);
}

/*
 * MsgOut2.
 */
void MsgOut2()
{
  LOGN("MsgOut2");
  SCSI_OUT(vMSG,  active) //  gpio_write(MSG, high);
  SCSI_OUT(vCD ,  active) //  gpio_write(CD, high);
  SCSI_OUT(vIO ,inactive) //  gpio_write(IO, low);
  m_msb[m_msc] = readHandshake();
  m_msc++;
  m_msc %= 256;
}

/*
 * メインループ.
 */
void loop()
{
  //int msg = 0;
  m_msg = 0;

  // RST=H,BSY=H,SEL=L になるまで待つ
  do {} while( SCSI_IN(vBSY) || !SCSI_IN(vSEL) || SCSI_IN(vRST));

	// BSY+ SEL-
  // 応答すべきIDがドライブされていなければ次を待つ
  //byte db = readIO();
  //byte scsiid = db & scsi_id_mask;
  byte scsiid = readIO() & scsi_id_mask;
  if((scsiid) == 0) {
    return;
  }
  LOGN("Selection");
  m_isBusReset = false;
  // セレクトされたらBSYを-にする
  SCSI_BSY_ACTIVE();     // BSY出力だけON , ACTIVE にする

  // 応答するTARGET-IDを求める
#if USE_DB2ID_TABLE
  m_id = db2scsiid[scsiid];
  //if(m_id==0xff) return;
#else
  for(m_id=7;m_id>=0;m_id--)
    if(scsiid & (1<<m_id)) break;
  //if(m_id<0) return;
#endif

  // SELがinactiveになるまで待つ
  while(isHigh(gpio_read(SEL))) {
    if(m_isBusReset) {
      goto BusFree;
    }
  }
  SCSI_TARGET_ACTIVE()  // (BSY),REQ,MSG,CD,IO 出力をON
  //
  if(isHigh(gpio_read(ATN))) {
    bool syncenable = false;
    int syncperiod = 50;
    int syncoffset = 0;
    m_msc = 0;
    memset(m_msb, 0x00, sizeof(m_msb));
    while(isHigh(gpio_read(ATN))) {
      MsgOut2();
    }
    for(int i = 0; i < m_msc; i++) {
      // ABORT
      if (m_msb[i] == 0x06) {
        goto BusFree;
      }
      // BUS DEVICE RESET
      if (m_msb[i] == 0x0C) {
        syncoffset = 0;
        goto BusFree;
      }
      // IDENTIFY
      if (m_msb[i] >= 0x80) {
      }
      // 拡張メッセージ
      if (m_msb[i] == 0x01) {
        // 同期転送が可能な時だけチェック
        if (!syncenable || m_msb[i + 2] != 0x01) {
          MsgIn2(0x07);
          break;
        }
        // Transfer period factor(50 x 4 = 200nsに制限)
        syncperiod = m_msb[i + 3];
        if (syncperiod > 50) {
          syncoffset = 50;
        }
        // REQ/ACK offset(16に制限)
        syncoffset = m_msb[i + 4];
        if (syncoffset > 16) {
          syncoffset = 16;
        }
        // STDR応答メッセージ生成
        MsgIn2(0x01);
        MsgIn2(0x03);
        MsgIn2(0x01);
        MsgIn2(syncperiod);
        MsgIn2(syncoffset);
        break;
      }
    }
  }

  LOG("Command:");
  SCSI_OUT(vMSG,inactive) // gpio_write(MSG, low);
  SCSI_OUT(vCD ,  active) // gpio_write(CD, high);
  SCSI_OUT(vIO ,inactive) // gpio_write(IO, low);

  int len;
  byte cmd[12];
  cmd[0] = readHandshake(); if(m_isBusReset) goto BusFree;
  LOGHEX(cmd[0]);
  // コマンド長選択、受信
  static const int cmd_class_len[8]={6,10,10,6,6,12,6,6};
  len = cmd_class_len[cmd[0] >> 5];
  cmd[1] = readHandshake(); LOG(":");LOGHEX(cmd[1]); if(m_isBusReset) goto BusFree;
  cmd[2] = readHandshake(); LOG(":");LOGHEX(cmd[2]); if(m_isBusReset) goto BusFree;
  cmd[3] = readHandshake(); LOG(":");LOGHEX(cmd[3]); if(m_isBusReset) goto BusFree;
  cmd[4] = readHandshake(); LOG(":");LOGHEX(cmd[4]); if(m_isBusReset) goto BusFree;
  cmd[5] = readHandshake(); LOG(":");LOGHEX(cmd[5]); if(m_isBusReset) goto BusFree;
  // 残りのコマンド受信
  for(int i = 6; i < len; i++ ) {
    cmd[i] = readHandshake();
    LOG(":");
    LOGHEX(cmd[i]);
    if(m_isBusReset) goto BusFree;
  }
  // LUN 確認
  m_lun = m_sts>>5;
  m_sts = cmd[1]&0xe0;      // ステータスバイトにLUNをプリセット
  // HDD Imageの選択
  m_img = (HDDIMG *)0; // 無し
  if( (m_lun <= NUM_SCSILUN) )
  {
    m_img = &(img[m_id][m_lun]); // イメージあり
    if(!(m_img->m_file.isOpen()))
      m_img = (HDDIMG *)0;       // イメージ不在
  }
  // if(!m_img) m_sts |= 0x02;            // LUNに対するイメージファイル不在
  //LOGHEX(((uint32_t)m_img));

  LOG(":ID ");
  LOG(m_id);
  LOG(":LUN ");
  LOG(m_lun);

  LOGN("");
  switch(cmd[0]) {
  case 0x00:
    LOGN("[Test Unit]");
    break;
  case 0x01:
    LOGN("[Rezero Unit]");
    break;
  case 0x03:
    LOGN("[RequestSense]");
    onRequestSenseCommand(cmd[4]);
    break;
  case 0x04:
    LOGN("[FormatUnit]");
    break;
  case 0x06:
    LOGN("[FormatUnit]");
    break;
  case 0x07:
    LOGN("[ReassignBlocks]");
    break;
  case 0x08:
    LOGN("[Read6]");
    m_sts |= onReadCommand((((uint32_t)cmd[1] & 0x1F) << 16) | ((uint32_t)cmd[2] << 8) | cmd[3], (cmd[4] == 0) ? 0x100 : cmd[4]);
    break;
  case 0x0A:
    LOGN("[Write6]");
    m_sts |= onWriteCommand((((uint32_t)cmd[1] & 0x1F) << 16) | ((uint32_t)cmd[2] << 8) | cmd[3], (cmd[4] == 0) ? 0x100 : cmd[4]);
    break;
  case 0x0B:
    LOGN("[Seek6]");
    break;
  case 0x12:
    LOGN("[Inquiry]");
    m_sts |= onInquiryCommand(cmd[4]);
    break;
  case 0x1A:
    LOGN("[ModeSense6]");
    m_sts |= onModeSenseCommand(cmd[1]&0x80, cmd[2] & 0x3F, cmd[4]);
    break;
  case 0x1B:
    LOGN("[StartStopUnit]");
    break;
  case 0x1E:
    LOGN("[PreAllowMed.Removal]");
    break;
  case 0x25:
    LOGN("[ReadCapacity]");
    m_sts |= onReadCapacityCommand(cmd[8]);
    break;
  case 0x28:
    LOGN("[Read10]");
    m_sts |= onReadCommand(((uint32_t)cmd[2] << 24) | ((uint32_t)cmd[3] << 16) | ((uint32_t)cmd[4] << 8) | cmd[5], ((uint32_t)cmd[7] << 8) | cmd[8]);
    break;
  case 0x2A:
    LOGN("[Write10]");
    m_sts |= onWriteCommand(((uint32_t)cmd[2] << 24) | ((uint32_t)cmd[3] << 16) | ((uint32_t)cmd[4] << 8) | cmd[5], ((uint32_t)cmd[7] << 8) | cmd[8]);
    break;
  case 0x2B:
    LOGN("[Seek10]");
    break;
  case 0x5A:
    LOGN("[ModeSense10]");
    onModeSenseCommand(cmd[1] & 0x80, cmd[2] & 0x3F, ((uint32_t)cmd[7] << 8) | cmd[8]);
    break;
#if X1TURBO_DTC510B
  case 0xc2:
    LOGN("[DTC510B setDriveParameter]");
    m_sts |= dtc510b_setDriveparameter();
    break;
#endif
  default:
    LOGN("[*Unknown]");
    m_sts |= 0x02;
    m_senseKey = 5;
    break;
  }
  if(m_isBusReset) {
     goto BusFree;
  }

  LOGN("Sts");
  SCSI_OUT(vMSG,inactive) // gpio_write(MSG, low);
  SCSI_OUT(vCD ,  active) // gpio_write(CD, high);
  SCSI_OUT(vIO ,  active) // gpio_write(IO, high);
  writeHandshake(m_sts);
  if(m_isBusReset) {
     goto BusFree;
  }

  LOGN("MsgIn");
  SCSI_OUT(vMSG,  active) // gpio_write(MSG, high);
  SCSI_OUT(vCD ,  active) // gpio_write(CD, high);
  SCSI_OUT(vIO ,  active) // gpio_write(IO, high);
  writeHandshake(m_msg);

BusFree:
  LOGN("BusFree");
  m_isBusReset = false;
  //SCSI_OUT(vREQ,inactive) // gpio_write(REQ, low);
  //SCSI_OUT(vMSG,inactive) // gpio_write(MSG, low);
  //SCSI_OUT(vCD ,inactive) // gpio_write(CD, low);
  //SCSI_OUT(vIO ,inactive) // gpio_write(IO, low);
  //SCSI_OUT(vBSY,inactive)
  SCSI_TARGET_INACTIVE() // BSY,REQ,MSG,CD,IO 出力をOFFにする
}
