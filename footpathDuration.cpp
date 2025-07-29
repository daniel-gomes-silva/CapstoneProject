#include <curl/curl.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;

struct Stop {
  std::string stop_id;
  double latitude;
  double longitude;
};

// Load metro stops from CSV file
std::vector<Stop> loadMetroStops(const std::string &filename) {
  std::vector<Stop> stops;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return stops;
  }
  std::string line;

  // Skip header line
  if (!std::getline(file, line)) {
    std::cerr << "Empty file: " << filename << std::endl;
    return stops;
  }

  while (std::getline(file, line)) {
    if (line.empty()) continue;  // Skip empty lines

    std::stringstream ss(line);
    std::string item;
    Stop stop;

    // Parse CSV:
    // stop_id,stop_code,stop_name,stop_desc,stop_lat,stop_lon,zone_id,stop_url
    std::getline(ss, stop.stop_id, ',');
    std::getline(ss, item, ',');  // skip stop_code
    std::getline(ss, item, ',');  // skip stop_name
    std::getline(ss, item, ',');  // skip stop_desc
    std::getline(ss, item, ',');  // stop_lat
    try {
      stop.latitude = std::stod(item);
    } catch (const std::exception &e) {
      std::cerr << "Error parsing latitude: " << item << std::endl;
      continue;
    }
    std::getline(ss, item, ',');  // stop_lon
    try {
      stop.longitude = std::stod(item);
    } catch (const std::exception &e) {
      std::cerr << "Error parsing longitude: " << item << std::endl;
      continue;
    }

    stops.push_back(stop);
  }

  return stops;
}

// Load STCP stops from CSV file
std::vector<Stop> loadStcpStops(const std::string &filename) {
  std::vector<Stop> stops;
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    return stops;
  }
  std::string line;

  // Skip header line
  if (!std::getline(file, line)) {
    std::cerr << "Empty file: " << filename << std::endl;
    return stops;
  }

  while (std::getline(file, line)) {
    if (line.empty()) continue;  // Skip empty lines

    std::stringstream ss(line);
    std::string item;
    Stop stop;

    // Parse CSV: stop_id,stop_code,stop_name,stop_lat,stop_lon,zone_id,stop_url
    std::getline(ss, stop.stop_id, ',');
    std::getline(ss, item, ',');  // skip stop_code
    std::getline(ss, item, ',');  // skip stop_name
    std::getline(ss, item, ',');  // stop_lat
    try {
      stop.latitude = std::stod(item);
    } catch (const std::exception &e) {
      std::cerr << "Error parsing latitude: " << item << std::endl;
      continue;
    }
    std::getline(ss, item, ',');  // stop_lon
    try {
      stop.longitude = std::stod(item);
    } catch (const std::exception &e) {
      std::cerr << "Error parsing longitude: " << item << std::endl;
      continue;
    }

    stops.push_back(stop);
  }

  return stops;
}

// Create destinations option string for a range to be used in API requests
std::string createDestinationsParam(int numCoordinates) {
  std::string destinations;
  for (int i = 1; i <= numCoordinates; i++) {
    if (i != 1) destinations += ";";
    destinations += std::to_string(i);
  }
  return destinations;
}

// Callback function to handle the response data
// This will be called by libcurl when data is received
size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                     std::string *output) {
  size_t totalSize = size * nmemb;
  output->append((char *)contents,
                 totalSize);  // Append the received data to the output string
  return totalSize;
}

// Get durations from one source to multiple destinations
std::vector<double> getDurationsFromSource(const std::vector<Stop> &stops,
                                           int sourceIndex, int destStart,
                                           int destEnd) {
  CURL *curl;
  CURLcode res;
  std::string readBuffer;

  // Build coordinates string for all stops
  std::string coordinates;
  coordinates += std::to_string(stops[sourceIndex].longitude) + "," +
                 std::to_string(stops[sourceIndex].latitude);
  for (int i = destStart; i <= destEnd; i++) {
    coordinates += ";";
    coordinates += std::to_string(stops[i].longitude) + "," +
                   std::to_string(stops[i].latitude);
  }

  // Create destinations option
  std::string destinations = createDestinationsParam(destEnd - destStart + 1);

  // Build OSRM URL
  std::string url = "http://127.0.0.1:5001/table/v1/walking/" + coordinates +
                    "?sources=0" + "&destinations=" + destinations;

  std::cout << "Getting durations from stop " << sourceIndex << " ("
            << stops[sourceIndex].stop_id << ") to destinations " << destStart
            << "-" << destEnd << std::endl;

  curl = curl_easy_init();  // Create a CURL handle
  if (curl) {
    // Set the URL for the HTTP request
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    // Set the callback function to handle response data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    // Set the string to store the received data
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    // Set time out for the request
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    // Perform the HTTP request
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    // Print the request URL and response for debugging
    // std::cout << "Request URL: " << url << std::endl;
    // std::cout << "Response: " << readBuffer << std::endl;

    if (res == CURLE_OK) {
      try {
        json response = json::parse(readBuffer);
        if (response.contains("durations") && !response["durations"].empty()) {
          std::vector<double> durations;
          for (const auto &dist : response["durations"][0]) {
            durations.push_back(dist.is_null() ? -1.0 : dist.get<double>());
          }
          return durations;
        }
      } catch (const std::exception &e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
      }
    } else {
      std::cerr << "Request failed: " << curl_easy_strerror(res) << std::endl;
    }
  }

  return std::vector<double>();
}

int main() {
  auto start = std::chrono::high_resolution_clock::now();

  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Load STCP and metro stops from CSV files
  std::vector<Stop> metroStops =
      loadMetroStops("./datasets/Porto/metro/GTFS/stops.txt");
  std::cout << "Loaded " << metroStops.size() << " metro stops" << std::endl;
  std::vector<Stop> stcpStops =
      loadStcpStops("./datasets/Porto/stcp/GTFS/stops.txt");
  std::cout << "Loaded " << stcpStops.size() << " stcp stops" << std::endl;
  // Combine both into one vector
  std::vector<Stop> allStops = metroStops;
  allStops.insert(allStops.end(), stcpStops.begin(), stcpStops.end());
  std::cout << "Total: " << allStops.size() << " stops" << std::endl;

  // Generate unique filename for output CSV file
  std::string baseFilename = "foot_durations";
  std::string extension = ".csv";
  std::string filename = baseFilename + extension;
  int counter = 2;

  // Check if file exists and generate new filename if needed
  // Used to avoid overwriting existing files
  while (std::ifstream(filename)) {
    filename = baseFilename + "_" + std::to_string(counter) + extension;
    counter++;
  }

  // Open output CSV file
  std::ofstream outFile(filename);
  outFile << "stop_id,stop_id,duration" << std::endl;

  const int MAX_DESTINATIONS = 99;  // Maximum destinations per request
  int totalStops = allStops.size();
  int totalRequests = 0;
  int failedRequests = 0;
  int totalStopPairs = 0;

  // Process each stop as source
  for (int sourceIndex = 0; sourceIndex < totalStops; sourceIndex++) {
    // Process destinations in batches of MAX_DESTINATIONS
    for (int destStart = sourceIndex + 1; destStart < totalStops;
         destStart += MAX_DESTINATIONS) {
      int destEnd = std::min(destStart + MAX_DESTINATIONS - 1, totalStops - 1);

      std::vector<double> durations =
          getDurationsFromSource(allStops, sourceIndex, destStart, destEnd);

      if (!durations.empty()) {
        // Write durations to CSV
        for (int i = 0; i < durations.size(); i++) {
          int destIndex = destStart + i;
          totalStopPairs++;
          if (durations[i] >= 0) {
            outFile << allStops[sourceIndex].stop_id << ","
                    << allStops[destIndex].stop_id << "," << durations[i]
                    << std::endl;
          } else {
            outFile << allStops[sourceIndex].stop_id << ","
                    << allStops[destIndex].stop_id << ",-1" << std::endl;
            std::cerr << "No route found between "
                      << allStops[sourceIndex].stop_id << " and "
                      << allStops[destIndex].stop_id << std::endl;
          }
        }
        totalRequests++;
        std::cout << "Completed request " << totalRequests << std::endl;
      } else {
        std::cerr << "Failed to get durations for source " << sourceIndex
                  << std::endl;
        failedRequests++;
      }
    }
  }

  outFile.close();
  curl_global_cleanup();

  std::cout << "Total pairs processed: " << totalStopPairs << std::endl;
  std::cout << "Total API requests made: " << totalRequests << std::endl;
  std::cout << "Total failed requests: " << failedRequests << std::endl;

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "Execution time: " << (elapsed.count() / 60.0) << " minutes"
            << std::endl;

  return 0;
}
