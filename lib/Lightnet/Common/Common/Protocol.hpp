#pragma once

#include <Arduino.h>

#define PACK __attribute__((__packed__))

namespace Protocol
{
	const uint16_t VERSION = 1;
	const uint8_t MAX_PACKET_SIZE = 32;
	const uint8_t POLLING_ADDRESS = 120;

	enum colorMode_t: uint8_t {
		COLOR_MODE_RGB
	};

	enum packetType_t: uint8_t {
		PACKET_INITIALIZATION_POLL = 1,
		PACKET_REGISTER_EDGE = 2,
		PACKET_TURN_ON_OFF = 3,
		PACKET_SET_COLOR = 4,
		PACKET_SET_BRIGHTNESS = 5,
		PACKET_SET_COLOR_AND_BRIGHTNESS = 6,
		PACKET_REGISTER_EDGE_ACK = 7,
		PACKET_GET_FIRST_PANEL_EDGE_INFO = 8,
		PACKET_GET_NEXT_PANEL_EDGE_INFO = 9,
		PACKET_PANEL_EDGE_INFO = 10,
		PACKET_ACK = 11,
		PACKET_RESET_DEVICE = 200,
	};

	typedef struct PACK {
		uint8_t r;
		uint8_t g;
		uint8_t b;
	} ColorRGB;

	typedef struct PACK {
		union {
			ColorRGB rgb;
		};
	} Color;

// BEGIN Common packet structures
	typedef struct PACK {
		packetType_t type;
		uint16_t protocolVersion;
	} PacketHeader;

	typedef struct PACK {
		PacketHeader header;
		uint16_t headerCrc;
	} PacketMeta;
// END

// BEGIN Packets definitions
	typedef struct PACK {
		PacketMeta meta;
		uint16_t panelIndex;
	} PacketInitializationPoll;

	typedef struct PACK {
		PacketMeta meta;
		uint16_t panelIndex;
		uint16_t edgeIndex;
	} PacketRegisterEdge;

	typedef struct PACK {
		PacketMeta meta;
		uint8_t on;
	} PacketTurnOnOff;

	typedef struct PACK {
		PacketMeta meta;
		Color color;
	} PacketSetColor;

	typedef struct PACK {
		PacketMeta meta;
		uint8_t brightness;
	} PacketSetBrightness;

	typedef struct PACK {
		PacketMeta meta;
		Color color;
		uint8_t brightness;
	} PacketSetColorAndBrightness;

	typedef struct PACK {
		PacketMeta meta;
		uint16_t panelIndex;
		uint8_t edgeIndex;
		uint16_t connectedPanelIndex;
	} PacketPanelEdgeInfo;
// END

	uint8_t validatePacket(void *packet, uint8_t size);
	void setPacketMeta(void *packet, packetType_t type);

	const uint8_t MIN_PACKET_SIZE = sizeof(PacketMeta);
}
