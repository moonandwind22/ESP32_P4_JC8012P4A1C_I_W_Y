#include <Arduino.h>
#include <unity.h>

#include "brief_protocol.h"
#include "brief_transport.h"

void test_protocol_wire_sizes_are_stable()
{
    TEST_ASSERT_EQUAL_UINT16(1, brief::kProtocolVersion);
    TEST_ASSERT_EQUAL_size_t(brief::kPacketHeaderWireSize, sizeof(brief::PacketHeader));
    TEST_ASSERT_EQUAL_size_t(brief::kSystemStatusWireSize, sizeof(brief::SystemStatus));
    TEST_ASSERT_EQUAL_size_t(brief::kWeatherPayloadWireSize, sizeof(brief::WeatherPayload));
    TEST_ASSERT_EQUAL_size_t(brief::kTflPayloadWireSize, sizeof(brief::TflPayload));
    TEST_ASSERT_EQUAL_size_t(brief::kNewsPayloadWireSize, sizeof(brief::NewsPayload));
    TEST_ASSERT_EQUAL_size_t(brief::kDashboardSnapshotWireSize, sizeof(brief::DashboardSnapshot));
    TEST_ASSERT_EQUAL_size_t(24, sizeof(brief_transport::FrameHeader));
}

void test_transport_crc32_known_vector()
{
    const uint8_t payload[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, brief_transport::crc32(payload, sizeof(payload)));
}

void test_transport_header_round_trip_metadata()
{
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    const brief_transport::FrameHeader header = brief_transport::make_header(
        static_cast<uint8_t>(brief::kMsgDashboardSnapshot),
        42,
        123456,
        payload,
        sizeof(payload)
    );

    TEST_ASSERT_EQUAL_HEX32(brief_transport::kFrameMagic, header.magic);
    TEST_ASSERT_EQUAL_UINT16(brief_transport::kFrameVersion, header.version);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(brief::kMsgDashboardSnapshot), header.type);
    TEST_ASSERT_EQUAL_UINT32(42, header.sequence);
    TEST_ASSERT_EQUAL_UINT32(123456, header.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), header.payload_size);
    TEST_ASSERT_EQUAL_HEX32(brief_transport::crc32(payload, sizeof(payload)), header.payload_crc32);
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_protocol_wire_sizes_are_stable);
    RUN_TEST(test_transport_crc32_known_vector);
    RUN_TEST(test_transport_header_round_trip_metadata);
    UNITY_END();
}

void loop() {}
