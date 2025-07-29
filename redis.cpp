#include <hiredis/hiredis.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Create a key for storing durations/distances between two stops
std::string makeKey(const std::string &stop_id1, const std::string &stop_id2) {
  return (stop_id1 < stop_id2) ? stop_id1 + ":" + stop_id2
                               : stop_id2 + ":" + stop_id1;
}

int main() {
  auto start = std::chrono::high_resolution_clock::now();

  // The `redisContext` type represents the connection
  // to the Redis server. Here, we connect to the
  // default host and port.
  redisContext *redisContext = redisConnect("127.0.0.1", 6379);

  // Check if the context is null or if a specific
  // error occurred.
  if (redisContext == nullptr || redisContext->err) {
    if (redisContext) {
      std::cerr << "Redis connection error: " << redisContext->errstr
                << std::endl;
      redisFree(redisContext);
    } else {
      std::cerr << "Can't allocate redis context" << std::endl;
    }
    return 1;
  }
  std::cout << "Connected to Redis" << std::endl;

  //std::ifstream file("foot_distances.csv");
  std::ifstream file("foot_durations.csv");
  if (!file.is_open()) {
    std::cerr << "Failed to open file" << std::endl;
    redisFree(redisContext);
    return 1;
  }

  std::string line;
  int count = 0;

  // Skip header line
  if (!std::getline(file, line)) {
    std::cerr << "Empty file" << std::endl;
    return 1;
  }

  // Read the CSV file line by line and store data in Redis
  // The CSV format is: stop_id,stop_id,distance
  while (std::getline(file, line)) {
    // Skip empty lines
    if (line.empty()) continue;

    std::stringstream ss(line);
    std::string stop_id1, stop_id2, distance;

    if (std::getline(ss, stop_id1, ',') && std::getline(ss, stop_id2, ',') &&
        std::getline(ss, distance)) {
      // Validate data
      if (stop_id1.empty() || stop_id2.empty() || distance.empty()) {
        continue;
      }
      std::string key = makeKey(stop_id1, stop_id2);

      // SET redis command to store the distance
      redisReply *reply = (redisReply *)redisCommand(
          redisContext, "SET %s %s", key.c_str(), distance.c_str());

      if (reply) {
        // Check if the command was successful
        if (reply->type == REDIS_REPLY_ERROR) {
          std::cerr << "Redis error: " << reply->str << std::endl;
        }
        freeReplyObject(reply);
      } else {
        std::cerr << "Redis command failed for key: " << key << std::endl;
      }

      count++;
      if (count % 100000 == 0) {
        std::cout << "Processed " << count << " entries..." << std::endl;
      }
    } else {
      std::cerr << "Invalid line format: " << line << std::endl;
    }
  }

  std::cout << "Total entries processed: " << count << std::endl;
  file.close();

  // Close the connection.
  redisFree(redisContext);

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;
  std::cout << "Execution time: " << (elapsed.count() / 60.0) << " minutes"
            << std::endl;

  return 0;
}
