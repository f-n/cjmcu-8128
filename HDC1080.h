#ifndef IAQ_HDC1080_H
#define IAQ_HDC1080_H

#include <memory>
#include <string>
#include <vector>

// HDC1080, see also https://github.com/jshnaidman/HDC1080/blob/master/src/HDC1080JS.cpp
class HDC1080 {
public:
    HDC1080(std::string i2c_dev_name, uint8_t hdc1080_addr);

    ~HDC1080();

    enum Commands : uint8_t {
        GET_DEVICE_ID = 0xff,
        GET_MANUFACTURER_ID = 0xfe,
        GET_SERIAL_NR_HIGH = 0xfb,
        GET_SERIAL_NR_MID = 0xfc,
        GET_SERIAL_NR_LOW = 0xfd,
        CONGIGURATION_REGISTER = 0x02,
        HUMIDITY_REGISTER = 0x01,
        TEMPERATURE_REGISTER = 0x00,
    };

    enum MeasurementResolution : uint8_t {
     	HDC1080_RESOLUTION_8BIT,
	    HDC1080_RESOLUTION_11BIT,
	    HDC1080_RESOLUTION_14BIT,       
    };

    float measure_humidity();
    float measure_temperature();

    uint16_t get_device_id();
    uint16_t get_manufacturer_id();
    uint32_t get_serial_number();
    void set_resolution(enum MeasurementResolution res_temperture, enum MeasurementResolution res_humidity);
    void heater_on();
    void heater_off();

private:
    const std::string i2c_dev_name;
    const uint8_t hdc1080_addr;
    int i2c_fd = -1;
    uint16_t device_id = 0;
    uint16_t manufacturer_id = 0;
    uint32_t serial_number = 0;

    void close_device();

    void init();

    void open_device();

    std::unique_ptr<std::vector<uint8_t>> read_data(size_t buffer_size);

    void read_deviceId();

    void read_manufacturerId();

    void read_serialNumber();

    uint16_t read_configRegister();

    void write_configRegister(uint16_t config);

    void reset();

    void write_data(uint8_t *buffer, size_t buffer_len);
};

#endif //IAQ_HDC1080_H