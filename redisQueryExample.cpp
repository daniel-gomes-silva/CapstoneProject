#include <hiredis/hiredis.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Create a key for storing durations between two stops
std::string makeKey(const std::string &stop_id1, const std::string &stop_id2) {
  return (stop_id1 < stop_id2) ? stop_id1 + ":" + stop_id2
                               : stop_id2 + ":" + stop_id1;
}

int main() {
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

  std::string stop_id1 = "BAR2";
  std::string stop_id2 = "5697";
  std::string key = makeKey(stop_id1, stop_id2);

  // GET command to retrieve the duration
  redisReply *reply =
      (redisReply *)redisCommand(redisContext, "GET %s", key.c_str());

  if (reply == nullptr) {
    std::cerr << "GET command failed for key: " << key << std::endl;
  } else if (reply->type == REDIS_REPLY_STRING) {
    std::cout << "Duration between " << stop_id1 << " and " << stop_id2 << ": "
              << reply->str << std::endl;
  } else {
    std::cout << "No duration found in Redis for the given stops.\n";
  }

  if (reply) freeReplyObject(reply);

  // Close the connection.
  redisFree(redisContext);

  return 0;
}
