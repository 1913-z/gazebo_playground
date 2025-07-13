//
// Created by lfc on 2021/3/1.
//

#ifndef SRC_GAZEBO_CSV_READER_HPP
#define SRC_GAZEBO_CSV_READER_HPP

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

class CsvReader {
 public:
  static bool ReadCsvFile(const std::string &file_name,
                          std::vector<std::vector<double>> &datas) {
    std::ifstream file(file_name);
    if (!file.is_open()) {
      std::cerr << "CsvReader: cannot open file " << file_name << "\n";
      return false;
    }

    std::string line;

    if (!std::getline(file, line)) {
      std::cerr << "CsvReader: empty file " << file_name << "\n";
      return false;
    }

    while (std::getline(file, line)) {

      if (line.empty()) continue;

      std::stringstream ss(line);
      std::vector<double> row;
      std::string token;
      bool valid = true;


      while (std::getline(ss, token, ',')) {
        try {
          row.push_back(std::stod(token));
        } catch (const std::exception &e) {
          std::cerr << "CsvReader: skip invalid line: " << line << "\n";
          valid = false;
          break;
        }
      }

      if (!valid) continue;


      if (row.size() == 3) {
        datas.push_back(std::move(row));
      } else {
        std::cerr << "CsvReader: skip line with wrong column count (" 
                  << row.size() << "): " << line << "\n";
      }
    }

    std::cerr << "CsvReader: loaded data size: " << datas.size() << "\n";
    return true;
  }
};

#endif  // SRC_GAZEBO_CSV_READER_HPP

