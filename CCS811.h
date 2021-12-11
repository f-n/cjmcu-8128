#ifndef IAQ_CCS811_H
#define IAQ_CCS811_H

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <thread>

// CCS811 interface per specifications in
// https://cdn.sparkfun.com/assets/learn_tutorials/1/4/3/CCS811_Datasheet-DS000459.pdf
class CCS811 {
public:
    CCS811(std::string i2c_dev_name, uint8_t ccs811_addr);

    ~CCS811();

    int read_sensors();

    /* The equivalent CO2 (eCO2) output range for CCS811 is from 400ppm to 8192ppm. 
       Values outside this range are clipped. */
    uint16_t get_co2();

    /* The Total Volatile Organic Compound (TVOC) output range for CCS811 is from 0ppb to 1187ppb. 
       Values outside this range are clipped. */
    uint16_t get_tvoc();

    int set_env_data(double rel_humidity, double temperature);

    struct MailboxInfo {
        uint8_t id;
        size_t size;
        bool readable;
        bool writeable;
    };

    enum Mailbox {
        STATUS,
        MEAS_MODE,
        ALG_RESULT_DATA,
        RAW_DATA,
        ENV_DATA,
        NTC,
        THRESHOLDS,
        BASELINE,
        HW_ID,
        HW_VERSION,
        FW_BOOT_VERSION,
        FW_APP_VERSION,
        ERROR_ID,
        SW_RESET
    };

    enum Commands {
        APP_START = 0xF4
    };

    uint8_t verbose = 0;

private:
    const std::string i2c_dev_name;
    const uint8_t ccs811_addr;
    int i2c_fd = -1;
    time_t last_measurement = 0;
    uint16_t co2 = 0;
    uint16_t tvoc = 0;
    uint8_t measurement_mode[1] = {0x00};
    uint8_t baseline[2] = {0x00, 0x00};

    MailboxInfo mailbox_info(Mailbox m) {
        // These values should correspond to the Mailbox values above.
        static MailboxInfo mailbox_info[] = {
                /* addr, size, readable, writeable */
                /* STATUS */    {0x0,  1, true,  false},
                /* MEAS_MODE */
                                {0x1,  1, true,  true},
                /* ALG_RESULT_DATA */
                                {0x2,  8, true,  false},
                /* RAW_DATA */
                                {0x3,  2, true,  false},
                /* ENV_DATA */
                                {0x5,  4, false, true},
                /* NTC */
                                {0x6,  4, true,  false},
                /* THRESHOLDS */
                                {0x10, 5, false, true},
                /* BASELINE */
                                {0x11, 2, true,  true},
                /* HW_ID */
                                {0x20, 1, true,  false},
                /* HW_VERSION */
                                {0x21, 1, true,  false},
                /* FW_BOOT_VERSION */
                                {0x23, 2, true,  false},
                /* FW_APP_VERSION */
                                {0x24, 2, true,  false},
                /* ERROR_ID */
                                {0xE0, 1, true,  false},
                /* SW_RESET */
                                {0xFF, 4, false, true}
        };

        return mailbox_info[m];
    }

    int init();

    void open_device();

    int set_measurement_mode();
    
    int read_baseline();

    int write_baseline();

    std::unique_ptr<std::vector<uint8_t>> read_mailbox(Mailbox m, uint32_t delay_mys=62500);

    int write_to_mailbox(Mailbox m, uint8_t *buffer, size_t buffer_len);

    int write_data(uint8_t *buffer, size_t buffer_len);

    int version_to_str(uint8_t version, char *buffer);

    void close_device() const;
};


#endif //IAQ_CCS811_H
