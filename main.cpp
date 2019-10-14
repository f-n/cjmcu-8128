#include "BMP280.h"
#include "CCS811.h"
#include "HDC1080.h"

#include <iomanip>

int main() {
#if 0
    HDC1080 hdc1080("/dev/i2c-1", 0x40);

    while (true) {
        float relative_humidity = hdc1080.measure_humidity();
        float t_hdc1080 = hdc1080.measure_temperature();

        std::cout << "T(HDC1080): " << std::fixed << std::setprecision(2) << t_hdc1080 << "°C";
        std::cout << "\tRH: " << std::fixed << std::setprecision(2) << relative_humidity << "%";
        std::cout << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
#endif

#if 0
    CCS811 ccs811("/dev/i2c-1", 0x5a);
    BMP280 bmp280("/dev/i2c-1", 0x76);

    while (true) {
        ccs811.read_sensors();
        bmp280.measure();

        double t_bmp20 = bmp280.get_temperature();

        std::cout << "\tT(BMP280): " << std::fixed << std::setprecision(2) << t_bmp20 << "°C";
        std::cout << "\tCO2: " << std::dec << ccs811.get_co2() << "ppm";
        std::cout << "\tTVOC: " << std::dec << ccs811.get_tvoc() << "ppm";
        std::cout << "\tPres: " << std::fixed << std::setprecision(2) << bmp280.get_pressure() << "hPa";
        std::cout << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
#endif

    CCS811 ccs811("/dev/i2c-1", 0x5a);
    HDC1080 hdc1080("/dev/i2c-1", 0x40);
    BMP280 bmp280("/dev/i2c-1", 0x76);

    while (true) {
        ccs811.read_sensors();
        bmp280.measure();

        float t_hdc1080 = hdc1080.measure_temperature();
        double t_bmp20 = bmp280.get_temperature();
        float relative_humidity = hdc1080.measure_humidity();
        
        std::cout << "T(HDC1080): " << std::fixed << std::setprecision(2) << t_hdc1080 << "°C";
        std::cout << "\tT(BMP280): " << std::fixed << std::setprecision(2) << t_bmp20 << "°C";
        std::cout << "\tRH: " << std::fixed << std::setprecision(2) << relative_humidity << "%";
        std::cout << "\tCO2: " << std::dec << ccs811.get_co2() << "ppm";
        std::cout << "\tTVOC: " << std::dec << ccs811.get_tvoc() << "ppm";
        std::cout << "\tPres: " << std::fixed << std::setprecision(2) << bmp280.get_pressure() << "hPa";
        std::cout << std::endl;

        ccs811.set_env_data(relative_humidity, (t_hdc1080 + t_bmp20) / 2);

        std::this_thread::sleep_for(std::chrono::seconds(20));
    }
}
