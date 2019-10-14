#include "CCS811.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// #define DBG

CCS811::CCS811(std::string i2c_dev_name, uint8_t ccs811_addr)
        : i2c_dev_name(std::move(i2c_dev_name)),
          ccs811_addr(ccs811_addr) {
    open_device();
    init();
}

CCS811::~CCS811() {
    close_device();
}

void CCS811::close_device() const { if (i2c_fd >= 0) close(i2c_fd); }

uint16_t CCS811::get_co2() {
    return co2;
}

uint16_t CCS811::get_tvoc() {
    return tvoc;
}

int CCS811::init() {
    if (verbose) {
        std::cout << "[CCS811] checking the hardware id..." << std::endl;
    }
    auto hw_id = read_mailbox(HW_ID);
    if (hw_id->front() != 0x81) {
        std::cerr << "[CCS811] Unrecognized hardware id 0x" << std::hex << (int) hw_id->front() << std::endl;
        throw "[CCS811] Invalid device id!";
    }

/*
    std::cout << "[CCS811] Resetting CCS811..." << std::endl;
    uint8_t reset_sequence[] = {0x11, 0xe5, 0x72, 0x8a};
    write_to_mailbox(SW_RESET, reset_sequence, 4);

    std::cout << "[CCS811] Sleeping for a second..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto hw_version = read_mailbox(HW_VERSION);
    char version_str[15];
    version_to_str(hw_version->front(), version_str);
    std::cout << "[CCS811] HW Version: " << version_str << std::endl;

    auto fw_boot_ver = read_mailbox(FW_BOOT_VERSION);
    version_to_str(fw_boot_ver->front(), version_str);
    std::cout << "[CCS811] FW Boot Version: " << version_str << "." << (int) fw_boot_ver->at(1) << std::endl;

    auto fw_app_ver = read_mailbox(FW_APP_VERSION);
    version_to_str(fw_app_ver->front(), version_str);
    std::cout << "[CCS811] FW Application Version: " << version_str << "." << (int) fw_app_ver->at(1) << std::endl;
*/
    if (verbose) {
        std::cout << "[CCS811] Starting..." << std::endl;
    }
    uint8_t buffer[] = {APP_START};
    if (write_data(buffer, 1) < 0) {
        throw "[CCS811] unable to start";
    }
    std::this_thread::sleep_for(std::chrono::microseconds(62500));

/*
    if (verbose) {
        std::cout << "[CCS811] Configuring measurement mode to Mode 1 - Constant power mode, measuring every 1 sec."
                  << std::endl;
    }
    uint8_t measurement_mode[] = {1 << 4};
*/
    if (verbose) {
        std::cout << "[CCS811] Configuring measurement mode to - Pulse heating mode IAQ measurement every 10 sec."
                  << std::endl;
    }
    uint8_t measurement_mode[] = {0x20};
    if (write_to_mailbox(MEAS_MODE, measurement_mode, 1) < 0) {
        throw "[CCS811] unable to set mode";        
    }
    std::this_thread::sleep_for(std::chrono::microseconds(15000));
    return 0;
}

void CCS811::open_device() {
    i2c_fd = open(i2c_dev_name.c_str(), O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "[CCS811] Unable to open" << i2c_dev_name << ". " << strerror(errno) << std::endl;
        throw 1;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, ccs811_addr) < 0) {
        std::cerr << "[CCS811] Failed to communicate with the device. " << strerror(errno) << std::endl;
        throw 1;
    }
}

std::unique_ptr<std::vector<uint8_t>> CCS811::read_mailbox(CCS811::Mailbox m, uint32_t delay_mys) {
    auto mbox_info = mailbox_info(m);

    if (!mbox_info.readable) {
        std::cerr << "[CCS811] Mailbox is not writeable!" << std::endl;
        return std::make_unique<std::vector<uint8_t>>();
    }

    // Select the mailbox.
    uint8_t mailbox_id_buf[] = {mbox_info.id};
    if (write_data(mailbox_id_buf, 1) < 0) {
        return std::make_unique<std::vector<uint8_t>>();
    }
    if (delay_mys > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_mys));
    }

    size_t buffer_len = mbox_info.size;
    auto *read_buffer = new uint8_t[buffer_len];
    auto bytes_read = read(i2c_fd, read_buffer, buffer_len);
    if (bytes_read != buffer_len) {
        std::cerr << "[CCS811] Failed to read from the device. Bytes read: " << bytes_read << std::endl;
        // TODO - Have better exceptions.
        return std::make_unique<std::vector<uint8_t>>();
    }

    auto result = std::make_unique<std::vector<uint8_t>>();
#ifdef DBG
    std::cerr << "[CCS811] Read: ";
#endif
    for (size_t i = 0; i < mbox_info.size; i++) {
        result->push_back(read_buffer[i]);
#ifdef DBG
        std::cerr << "0x" << std::hex << (int)read_buffer[i] << " ";
#endif
    }
#ifdef DBG
    std::cerr << std::endl;
#endif

    delete[] read_buffer;
    return std::move(result);
}

int CCS811::read_sensors() {
    auto status = read_mailbox(STATUS);
    // Check if the sensor is ready for a read.
    if (!(status->front() & 8)) {
        std::cerr << "[CCS811] Device isn't ready yet." << std::endl;
        return -1;
    }

    if ((status->front() & 1) != 0) {
        auto error_register = read_mailbox(ERROR_ID);
        std::cerr << "[CCS811] Error detected. Error register: " << std::hex << error_register->front() << std::endl;
        return -1;
    }

    if ((status->front() & 0x08) != 8) {
        std::cerr << "[CCS811] No new samples are ready. Status register: " << std::hex << int(status->front()) << std::endl;
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(15000));
    auto data = read_mailbox(ALG_RESULT_DATA);
    int status_byte = data->at(4);
    int err_byte = data->at(5);

    if (status_byte != 0x98) {
        std::cerr << "[CCS811] Sensor wasn't ready (0x" << std::hex << status_byte << "). Not updatingmeasurements." << std::endl;
        return -1;
    }

    if (err_byte != 0) {
        std::cerr << "[CCS811] Error occurred while taking measurements. ERROR_ID: 0x" << std::hex << err_byte
                  << std::endl;
        return -1;
    }

    co2 = (data->at(0) << 8) | data->at(1);
    tvoc = (data->at(2) << 8) | data->at(3);

    // Mask out the 16th bit from measurements. Sensor can randomly set values with the 16th bit set.
    co2 &= ~(1 << 15);
    tvoc &= ~(1 << 15);

    last_measurement = time(nullptr);
    return 0;
}

int CCS811::write_data(uint8_t *buffer, size_t buffer_len) {
#ifdef DBG
    std::cout << "[CCS811] Write: ";
     for (size_t i = 0; i < buffer_len; i++) {
      std::cout << "0x" << std::hex << (int)buffer[i] << " ";
     }
    std::cout << std::endl;
#endif

    auto write_c = write(i2c_fd, buffer, buffer_len);
    if (write_c < 0) {
        std::cerr << "[CCS811] Unable to send command." << std::endl;
        // TODO - Have better exceptions.
        return -1;
    }
#ifdef DBG
    std::cout << "[CCS811]   ... wrote " << write_c << " bytes." << std::endl;
#endif
    return 0;
}

int CCS811::write_to_mailbox(CCS811::Mailbox m, uint8_t *buffer, size_t buffer_len) {
    auto mbox_info = mailbox_info(m);
    if (!mbox_info.writeable) {
        std::cerr << "[CCS811] Mailbox is not writeable!" << std::endl;
        // TODO
        return -1;
    }

    // Make sure that we leave room for the mailbox address and that we don't
    // write more than the mailbox can take.
    auto write_buf_len = std::min(buffer_len, mbox_info.size) + 1;
    uint8_t write_buffer[write_buf_len];
    write_buffer[0] = mbox_info.id;
    memcpy(&write_buffer[1], buffer, write_buf_len);

    return write_data(write_buffer, write_buf_len);
}

// This is pretty unsafe.
int CCS811::version_to_str(uint8_t version, char *buffer) {
    int major = version >> 4;
    int minor = version & 0xF;
    return sprintf(buffer, "%d.%d", major, minor);
}

int CCS811::set_env_data(double rel_humidity, double temperature) {
    auto rh_data = static_cast<uint16_t>(rel_humidity * 512);
    auto temp_data = static_cast<uint16_t>((temperature + 25) * 512);
    uint8_t env_data[] = {static_cast<uint8_t>(rh_data >> 8), static_cast<uint8_t>(rh_data & 0xFF),
                          static_cast<uint8_t>(temp_data >> 8), static_cast<uint8_t>(temp_data & 0xFF)};
    
    return write_to_mailbox(ENV_DATA, env_data, 4);
}
