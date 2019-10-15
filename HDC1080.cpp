#include "HDC1080.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <iostream>
#include <chrono>
#include <thread>

// #define DBG

HDC1080::HDC1080(std::string i2c_dev_name, uint8_t hdc1080_addr)
        : i2c_dev_name(std::move(i2c_dev_name)),
          hdc1080_addr(hdc1080_addr) {
    open_device();
    init();
}

HDC1080::~HDC1080() {
    close_device();
}

void HDC1080::close_device() { if (i2c_fd >= 0) close(i2c_fd); }

void HDC1080::init() {
    uint16_t config;
    reset();

    if (read_manufacturerId() < 0) {
        throw "[HDC1080] unable to get Manufacturer ID";
    }
    if (manufacturer_id != 0x5449) {
        throw "[HDC1080] wrong Manufacturer ID";
    }
    if (read_deviceId() < 0) {
        throw "[HDC1080] unable to get Device ID";
    }
    if (device_id != 0x1050) {
        throw "[HDC1080] wrong Device ID";
    }
    if (read_serialNumber() < 0) {
        throw "[HDC1080] unable to get Serial Number";
    }
    heater_off();
    int res = set_resolution(HDC1080_RESOLUTION_11BIT, HDC1080_RESOLUTION_11BIT);

    config = read_configRegister();

    if (verbose) {
        std::cout << "[HDC1080] Manufacturer ID: 0x" << std::hex << manufacturer_id << ", Device ID: 0x" << device_id;
        std::cout << ", Serial Nr: 0x" << serial_number << ", Configuration Register: 0x" << config << std::endl;
    }
}

void HDC1080::open_device() {
    i2c_fd = open(i2c_dev_name.c_str(), O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "[HDC1080] Unable to open" << i2c_dev_name << ". " << strerror(errno) << std::endl;
        throw 1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, hdc1080_addr) < 0) {
        std::cerr << "[HDC1080] Failed to communicate with the device. " << strerror(errno) << std::endl;
        throw 1;
    }
}

uint16_t HDC1080::get_device_id() {
    return device_id;
}

uint16_t HDC1080::get_manufacturer_id() {
    return manufacturer_id;
}

uint32_t HDC1080::get_serial_number() {
    return serial_number;
}

int HDC1080::write_data(uint8_t *buffer, size_t buffer_len) {
#ifdef DBG
    std::cout << "[HDC1080] Write: ";
    for (size_t i = 0; i < buffer_len; i++) {
        std::cout << "0x" << std::hex << (int) buffer[i] << " ";
    }
    std::cout << std::endl;
#endif

    auto write_c = write(i2c_fd, buffer, buffer_len);
    if (write_c < 0) {
        std::cerr << "Unable to send command. Retcode: " << write_c << std::endl;
        // TODO - Have better exceptions.
        return -1;
    }
#ifdef DBG
    std::cout << "[HDC1080]   ... wrote " << write_c << " bytes." << std::endl;
#endif
    return 0;
}

std::unique_ptr<std::vector<uint8_t>> HDC1080::read_data(size_t buffer_size) {
    auto *read_buffer = new uint8_t[buffer_size];
    auto bytes_read = read(i2c_fd, read_buffer, buffer_size);

#ifdef DBG
    std::cout << "[HDC1080] Read " << std::dec << bytes_read << " bytes" << std::endl;
#endif
    if (bytes_read < 0) {
        // return an empty vector if we can't read anything.
        return std::make_unique<std::vector<uint8_t>>();
    }

    auto result = std::make_unique<std::vector<uint8_t>>(read_buffer, read_buffer + bytes_read);

    delete[] read_buffer;

#ifdef DBG
    std::cout << "[HDC1080] Read: ";
    for (auto e : *result) {
        std::cout << std::hex << (int) e << " ";
    }
    std::cout << std::endl;
#endif
    return result;
}

int HDC1080::reset() {
    uint16_t config = read_configRegister();
    config = (config | 0x8000); // set reset bit
    return write_configRegister(config);
}

int HDC1080::read_deviceId() {
    uint8_t cmd[] = {GET_DEVICE_ID};
    if (write_data(cmd, 1) < 0) {
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    device_id = response->at(0) * 256 + response->at(1);
    return 0;
}

int HDC1080::read_manufacturerId() {
    uint8_t cmd[] = {GET_MANUFACTURER_ID};
    if (write_data(cmd, 1) < 0) {
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    manufacturer_id = response->at(0) * 256 + response->at(1);
}

int HDC1080::read_serialNumber() {
    uint32_t serialNumber = 0;
    uint8_t cmd_h[] = {GET_SERIAL_NR_HIGH};
    uint8_t cmd_m[] = {GET_SERIAL_NR_MID};
    uint8_t cmd_l[] = {GET_SERIAL_NR_LOW};
    if (write_data(cmd_h, 1) < 0) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    serialNumber = response->at(0) * 256 + response->at(1);

    if (write_data(cmd_m, 1) < 0) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    response = read_data(2);

    serialNumber = serialNumber * 256 + response->at(0) * 256 + response->at(1);

    if (write_data(cmd_l, 1) < 0) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    response = read_data(2);

    serialNumber = serialNumber * 256 + response->at(0) * 256 + response->at(1);

    serial_number = serialNumber;

    return 0;
}

uint16_t HDC1080::read_configRegister() {
    uint8_t cmd[] = {CONGIGURATION_REGISTER};
    if (write_data(cmd, 1) < 0) {
        return 0;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    return response->at(0) * 256 + response->at(1);
}

int HDC1080::write_configRegister(uint16_t config) {
    uint8_t cmd[] = {CONGIGURATION_REGISTER, (uint8_t)(config>>8), 0x00};
    if (write_data(cmd, 3) < 0) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(15000));
    return 0;
}

int HDC1080::set_resolution(enum MeasurementResolution res_temperture, enum MeasurementResolution res_humidity) {
    uint16_t config = read_configRegister();
    // temperature:
    config = (config & ~0x0400);
    if (res_temperture == HDC1080_RESOLUTION_11BIT) {
        config |= 0x0400;
    }
    // humidity:
    config = (config & ~0x0300);
    if (res_humidity == HDC1080_RESOLUTION_11BIT) {
        config |= 0x0100;
    } else if (res_humidity == HDC1080_RESOLUTION_8BIT) {
        config |= 0x0200;    
    }
   
    return write_configRegister(config);
 }

int HDC1080::set_acquisition(uint8_t value) {
    /* set acquisition bit in config register:
        value  = 0 -> register set to 0 -> measurement mode: temperature OR humidity
        value != 0 -> register set to 1 -> measurement mode: temperature AND humidity
    */
    uint16_t config = read_configRegister();
    if (value == 0) {
        config = (config & ~(0x1000));
    } else {
        config = (config | 0x1000);
    }
    return write_configRegister(config);
}

float HDC1080::get_recent_humidity() {
    return recent_humidity;
}

float HDC1080::get_recent_temperature() {
    return recent_temperature;
}

float HDC1080::measure_humidity() {
    uint8_t cmd[] = {HUMIDITY_REGISTER};

    if (set_acquisition(0) < 0) {
        return recent_humidity; // fallback to old value
    }

    if (write_data(cmd, 1) < 0) {
        return recent_humidity; // fallback to old value
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    uint16_t raw = response->at(0) * 256 + response->at(1);

    recent_humidity = ((float)raw) *100/65536;
    return recent_humidity;
}

float HDC1080::measure_temperature() {
    uint8_t cmd[] = {TEMPERATURE_REGISTER};

    if (set_acquisition(0) < 0) {
        return recent_temperature; // fallback to old value
    }
    if (write_data(cmd, 1) < 0) {
        return recent_temperature; // fallback to old value
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(2);

    uint16_t raw = response->at(0) * 256 + response->at(1);

    recent_temperature = ((float)raw) *165/65536 - 40;
    return recent_temperature;
}

int HDC1080::measure_temperature_and_humidity() {
    uint8_t cmd[] = {TEMPERATURE_REGISTER};

    if (set_acquisition(1) < 0) {
        return -1;
    }
    if (write_data(cmd, 1) < 0) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(62500));
    auto response = read_data(4);

    uint16_t raw = response->at(0) * 256 + response->at(1);
    recent_temperature = ((float)raw) *165/65536 - 40;

    raw = response->at(2) * 256 + response->at(3);
    recent_humidity = ((float)raw) *100/65536;

    return 0;
}

int HDC1080::heater_on() {
    uint16_t config = read_configRegister();

    config = (config | 0x2000);
   
    return write_configRegister(config);
 }

int HDC1080::heater_off() {
    uint16_t config = read_configRegister();

    config = (config  &  ~(0x2000));
   
    return write_configRegister(config);
 }
