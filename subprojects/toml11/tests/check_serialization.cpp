#include <toml.hpp>

#include <iomanip>
#include <iostream>

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        std::cerr << "usage: ./check [filename]" << std::endl;
        return 1;
    }

    const std::string filename(argv[1]);

    {
        const auto data = toml::parse(filename);
        {
            std::ofstream ofs("tmp.toml");
            ofs << std::setprecision(16) << std::setw(80) << data;
        }
        const auto serialized = toml::parse("tmp.toml");

        if(data != serialized)
        {
            // this is really a ditry hack, but is the easiest way...
            // TODO: cleanup by adding comparison function to check if a value is NaN or not
            if(filename.substr(filename.size() - 22, 22) == "float-inf-and-nan.toml" &&
                std::isnan   (toml::find<double>(serialized, "nan"))           &&
               !std::signbit (toml::find<double>(serialized, "nan"))           &&
                std::isnan   (toml::find<double>(serialized, "nan_plus"))      &&
               !std::signbit (toml::find<double>(serialized, "nan_plus"))      &&
                std::isnan   (toml::find<double>(serialized, "nan_neg"))       &&
                std::signbit (toml::find<double>(serialized, "nan_neg"))       &&
               !std::isnan   (toml::find<double>(serialized, "infinity"))      &&
               !std::isfinite(toml::find<double>(serialized, "infinity"))      &&
               !std::signbit (toml::find<double>(serialized, "infinity"))      &&
               !std::isnan   (toml::find<double>(serialized, "infinity_plus")) &&
               !std::isfinite(toml::find<double>(serialized, "infinity_plus")) &&
               !std::signbit (toml::find<double>(serialized, "infinity_plus")) &&
               !std::isnan   (toml::find<double>(serialized, "infinity_neg"))  &&
               !std::isfinite(toml::find<double>(serialized, "infinity_neg"))  &&
                std::signbit (toml::find<double>(serialized, "infinity_neg")))
            {
                // then it is correctly serialized.
                // Note that, the result of (nan == nan) is false. so `data == serialized` is false.
            }
            else
            {
                std::cerr << "============================================================\n";
                std::cerr << "result (w/o comment) different: " << filename << std::endl;
                std::cerr << "------------------------------------------------------------\n";
                std::cerr << "# serialized\n";
                std::cerr << serialized;
                std::cerr << "------------------------------------------------------------\n";
                std::cerr << "# data\n";
                std::cerr << data;
                return 1;
            }
        }
    }
    {
        const auto data = toml::parse<toml::preserve_comments>(filename);
        {
            std::ofstream ofs("tmp.toml");
            ofs << std::setprecision(16) << std::setw(80) << data;
        }
        const auto serialized = toml::parse<toml::preserve_comments>("tmp.toml");
        if(data != serialized)
        {
            // this is really a ditry hack, but is the easiest way...
            // TODO: cleanup by adding comparison function to check if a value is NaN or not
            if(filename.substr(filename.size() - 22, 22) == "float-inf-and-nan.toml" &&
                std::isnan   (toml::find<double>(serialized, "nan"))           &&
               !std::signbit (toml::find<double>(serialized, "nan"))           &&
                std::isnan   (toml::find<double>(serialized, "nan_plus"))      &&
               !std::signbit (toml::find<double>(serialized, "nan_plus"))      &&
                std::isnan   (toml::find<double>(serialized, "nan_neg"))       &&
                std::signbit (toml::find<double>(serialized, "nan_neg"))       &&
               !std::isnan   (toml::find<double>(serialized, "infinity"))      &&
               !std::isfinite(toml::find<double>(serialized, "infinity"))      &&
               !std::signbit (toml::find<double>(serialized, "infinity"))      &&
               !std::isnan   (toml::find<double>(serialized, "infinity_plus")) &&
               !std::isfinite(toml::find<double>(serialized, "infinity_plus")) &&
               !std::signbit (toml::find<double>(serialized, "infinity_plus")) &&
               !std::isnan   (toml::find<double>(serialized, "infinity_neg"))  &&
               !std::isfinite(toml::find<double>(serialized, "infinity_neg"))  &&
                std::signbit (toml::find<double>(serialized, "infinity_neg"))  &&
                toml::find(data, "nan").comments()           == toml::find(serialized, "nan").comments()           &&
                toml::find(data, "nan_plus").comments()      == toml::find(serialized, "nan_plus").comments()      &&
                toml::find(data, "nan_neg").comments()       == toml::find(serialized, "nan_neg").comments()       &&
                toml::find(data, "infinity").comments()      == toml::find(serialized, "infinity").comments()      &&
                toml::find(data, "infinity_plus").comments() == toml::find(serialized, "infinity_plus").comments() &&
                toml::find(data, "infinity_neg").comments()  == toml::find(serialized, "infinity_neg").comments()  )
            {
                // then it is correctly serialized.
                // Note that, the result of (nan == nan) is false. so `data == serialized` is false.
            }
            else
            {
                std::cerr << "============================================================\n";
                std::cerr << "result (w/  comment) different: " << filename << std::endl;
                std::cerr << "------------------------------------------------------------\n";
                std::cerr << "# serialized\n";
                std::cerr << serialized;
                std::cerr << "------------------------------------------------------------\n";
                std::cerr << "# data\n";
                std::cerr << data;
                return 1;
            }
        }
    }
    return 0;
}
