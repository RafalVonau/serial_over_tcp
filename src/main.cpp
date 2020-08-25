#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
  #include "user_interface.h"
}
#include <ESPAsyncTCP.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

#define HOSTNAME "serial_client"
#define USE_WIFI_MANAGER
#ifndef USE_WIFI_MANAGER
const char *ssid = "XXXXX";
const char *password = "XXXXXX";
#endif

#define serialSpeed        (2000000ul)
//#define serialSpeed        (115200ul)


/* Character timeout for end of frame detection */
#define characterBreakTime (5ul + 2*(10000000ul/serialSpeed))
#define BUFFER_SIZE (8 * 1024)
//#define BUFFER_SIZE (128)
#define RX_BUFFER_TH (1500)


class RingBuffer
{
public:
	RingBuffer(size_t capacity) {
		/* One byte is used for detecting the full condition. */
		m_size   = capacity + 1;
		m_buf    = new char[m_size];
		m_bufend = m_buf + m_size;
		reset();
	}
	~RingBuffer()       { delete [] m_buf; }
	void reset()        { m_head = m_tail = m_buf; }
	size_t size()       { return m_size; }
	size_t capacity()   { return m_size - 1; }
	size_t bytes_free() { if (m_head >= m_tail) return (capacity() - (m_head - m_tail)); else return (m_tail - m_head - 1); }
	size_t bytes_used() { return capacity() - bytes_free(); }
	bool is_full()      { return bytes_free() == 0; }
	bool is_empty()     { return bytes_free() == capacity(); }

	/*!
	 * \brief Write data to ring buffer.
	 */
	size_t write(const char *src, size_t count) {
		size_t nread = 0;
		if (!m_buf) return 0;
		//if (count > bytes_free()) count = bytes_free();
		while (nread != count) {
			/* don't copy beyond the end of the buffer */
			size_t n = min((size_t)(m_bufend - m_head), (count - nread));
			os_memcpy(m_head, src, n);
			m_head += n;
			nread  += n;
			src    += n;
			/* wrap? */
			if (m_head == m_bufend) m_head = m_buf;
		}
		return nread;
	}

	/*!
	 * \brief Read data from ring buffer.
	 */
	size_t read(char *dst, size_t count) {
		size_t nwritten = 0;
		size_t bytesused = bytes_used();

		if (!m_buf) return 0;
		if (count > bytesused) count = bytesused;
		while (nwritten != count) {
			size_t n = min((size_t)(m_bufend - m_tail), (count - nwritten));
			os_memcpy(dst, m_tail, n);
			m_tail   += n;
			nwritten += n;
			dst      += n;
			/* wrap ? */
			if (m_tail == m_bufend) m_tail = m_buf;
		}
		return nwritten;
	}

	/*!
	 * \brief Fill buffer from serial port.
	 */
	size_t fillFromSerial() {
		size_t count, nread = 0;
		
		count = Serial.available();

		/* sanitary checks */
		if (!m_buf) return 0;
		if (count > bytes_free()) count = bytes_free();
		if (count == 0) return 0;

		while (nread != count) {
			size_t n = min((size_t)(m_bufend - m_head), (count - nread));
			Serial.read(m_head, n);
			m_head += n;
			nread  += n;
			/* wrap? */
			if (m_head == m_bufend) m_head = m_buf;
		}
		return nread;
	}

	/*!
	 * \brief Flush buffer to serial port.
	 */
	bool flushToSerial() {
		size_t count, bytesused, nwritten = 0;
		
		/* sanitary checks */
		if (!m_buf) return false;
		bytesused = bytes_used();
		count     = Serial.availableForWrite();
		if (count > bytesused) count = bytesused;
		if (count == 0) return false;
		/* Ok - flush data to serial */
		while (nwritten != count) {
			size_t n = min((size_t)(m_bufend - m_tail), (count - nwritten));
			Serial.write(m_tail, n);
			m_tail   += n;
			nwritten += n;
			/* wrap ? */
			if (m_tail == m_bufend) m_tail = m_buf;
		}
		return true;
	}

	bool flushToTCP(AsyncClient* client) {
		size_t count, bytesused, nwritten = 0;
		/* sanitary checks */
		if ((!m_buf) || (!client))  return false;
		if (!client->canSend()) return false;
		bytesused  = bytes_used();
		count      = client->space();
		if (count > bytesused) count = bytesused;
		if (count == 0) return false;
		/* Ok - flush data to TCP */
		while (nwritten != count) {
			size_t n = min((size_t)(m_bufend - m_tail), (count - nwritten));
			client->add(m_tail, n);
			m_tail   += n;
			nwritten += n;
			/* wrap ? */
			if (m_tail == m_bufend) m_tail = m_buf;
		}
		client->send();
		return true;
	}

private:
	size_t m_size;
	char  *m_buf;
	char  *m_head;
	char  *m_tail;
	char  *m_bufend;
};
//==========================================================================================



AsyncServer  *m_tcp;
AsyncClient  *m_client;
RingBuffer   *m_tx_buffer;
RingBuffer   *m_rx_buffer;
uint64_t      m_previousMicros = 0;          /*!< Previous micros for timeout computations      */
size_t        m_lastBytes = 0;               /*!< Last avail bytes for timeout computations     */


void setup() {
 	pinMode(D4,OUTPUT);
	Serial.begin(serialSpeed);
	digitalWrite(D4, LOW);
	/* Connect to wifi */
	WiFi.hostname(HOSTNAME);
#ifdef USE_WIFI_MANAGER	
	WiFiManager wifiManager;	
	wifiManager.autoConnect("SerialOverTCP");	
#else
	WiFi.mode ( WIFI_STA );
	WiFi.begin ( ssid, password );
	// Wait for connection
	while ( WiFi.status() != WL_CONNECTED ) {
		delay ( 500 );
	}
#endif	  
  	Serial.print("IP address: ");
  	Serial.println(WiFi.localIP());
	/* Setup OTA */
	ArduinoOTA.setHostname(HOSTNAME);
	ArduinoOTA.begin();
	/* */
	m_previousMicros = 0;          /*!< Previous micros for timeout computations      */
	m_lastBytes      = 0;          /*!< Last avail bytes for timeout computations     */
	m_tx_buffer      = new RingBuffer(BUFFER_SIZE);
	m_rx_buffer      = new RingBuffer(BUFFER_SIZE);
	m_tcp            = new AsyncServer(1234); // start listening on tcp port 1234
	m_client         = nullptr;
	m_tcp->onClient([](void* arg, AsyncClient* client) {
		m_client = client;
		client->setNoDelay(true);
		client->onData([](void* arg, AsyncClient* client, void *data, size_t len) {m_tx_buffer->write((char *)data,len);}, NULL);
		client->onDisconnect([](void* arg, AsyncClient* client) {
			if (m_client == client) m_client = nullptr;
			client->close(true);
			client->free();
			delete client;
		}, NULL);
	}, NULL);
	m_tcp->begin();
	digitalWrite(D4, HIGH);
}
//==========================================================================================

/*!
 * \brief The main loop.
 * 1) Send packets from TX queue over uart port.
 * 2) Collect data from uart and push it to RX queue.
 */
void loop() {
	uint64_t currentMicros;
	size_t avail;
	bool doTx = false;

	
	/* Send data from TX queue */
	m_tx_buffer->flushToSerial();

	/* Collect data and push it to RX queue */
	m_rx_buffer->fillFromSerial();
	currentMicros = micros64();
	avail = m_rx_buffer->bytes_used();
	if (avail) {
		if (avail < RX_BUFFER_TH) {
			if (avail > m_lastBytes) {
				m_previousMicros = currentMicros;
				m_lastBytes      = avail;
			} else if ((currentMicros - m_previousMicros) > characterBreakTime) {
				/* Transmision break timeout  */
				doTx = true;
			}
		} else {
			/* Buffer is full */
			doTx = true;
		}
	}
	if (doTx) {
		digitalWrite(D4, LOW);	
		m_rx_buffer->flushToTCP(m_client);
		m_previousMicros = currentMicros;
		m_lastBytes      = m_rx_buffer->bytes_used();
		digitalWrite(D4, HIGH);	
	}
	ArduinoOTA.handle();
}
//==========================================================================================


