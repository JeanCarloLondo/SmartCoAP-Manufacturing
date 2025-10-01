# SmartCoAP-Manufacturing

## Introduction
This project implements the Constrained Application Protocol (CoAP) in a **smart manufacturing scenario**, combining a concurrent CoAP server written in C and two types of clients:
1. **ESP32-based sensor simulators**, which periodically send sensor data (temperature, humidity).
2. **A console-based client application**, which allows querying and managing stored data.

The main goal is to provide hands-on experience with the **design and implementation of an application-layer protocol** using the Berkeley sockets API over UDP, deployed in a cloud environment (AWS EC2).  
This project reinforces key networking concepts such as client-server communication, concurrency, message serialization, and IoT integration.

**Team members:**

- Jean Carlo Londoño Ocampo
- Emanuel Gonzales Quintero
- Juan Felipe Martinez 

---

## Development
The implementation was divided into three major components: **protocol**, **server**, and **clients**.

### 1. Protocol Implementation
- Protocol: **CoAP (RFC 7252)**.
- Transport: **UDP**, using the Berkeley sockets API.
- Message types:  
  - Confirmable (CON)  
  - Non-confirmable (NON)  
  - Acknowledgement (ACK)  
  - Reset (RST)  
- Methods supported: **GET, POST, PUT, DELETE**.  
- Additional features:  
  - Message ID for correlation.  
  - Token support to link requests and responses.  
  - URI-Path option handling.  
  - JSON payloads for sensor data (`{"temp":X,"hum":Y}`).

### 2. CoAP Server
- Implemented in **C**, fully based on the Berkeley sockets API.  
- Deployed in an **AWS EC2 instance** (Ubuntu 22.04).  
- Supports **concurrent clients** using **POSIX threads (pthreads)**.  
- Features a **SQLite database** for persistent storage of sensor records.  
- Logging system implemented to record all incoming requests and responses in `server.log`.  
- Command to run:  
  ```
  ./coap_server <PORT> <LogFile> || make server
  ```

### 3. Client Applications

**ESP32 Simulator (esp32_sim):**

- Generates periodic sensor data.

- Sends POST requests to the server with configurable number of devices, request rate, and interval.

- Useful for stress-testing the server.

- Command to run:
  
  ```
  ./esp32_sim <dirIP> <PORT> <msgs> <interval> || make esp32_sim
  ```
  
**Console Client (client):**

- Interactive CLI supporting all CoAP methods.

- Command to run:
  
  ```
  ./client <dirIP> <PORT> <sensor_number> <message id> || make client
  ```

**Example commands:

  ```
GET all
POST "temp":22,"hum":60
PUT 1="temp":25
DELETE 1
  ```

---

### 4. Deployment and Testing

- The server listens on the public IP of the AWS EC2 instance.

- Connectivity verified by sending CoAP requests from another independent Linux instance.

- tcpdump was used during debugging to confirm UDP packets were reaching the server.

#### Testing

The project includes multiple test cases to validate the functionality of the CoAP protocol implementation, the database layer, and the concurrency behavior of the server. All tests are defined in the `tests/` directory and can be compiled and executed using the `Makefile`.

##### 1. Compiling Tests

To compile all tests:
```bash
make test
```

**This command builds all test binaries into the build/bin/ directory.**

##### 2. Running Tests

**Tests are executed with:**

```bash
make run TEST=<test_name>
```
**Where <test_name> corresponds to the name of the test file (without extension) or a special target. Below are the main available tests:**

**a) Protocol Request Tests**

**Example:**

```bash
make run TEST=test_req001
```
**Purpose: Validates the serialization and parsing of CoAP messages (GET, POST, PUT, DELETE).**

**Output: Shows the step-by-step message exchange to confirm protocol compliance.**

**b) Database Test**

**Example:**

```bash
make run TEST=db_test
```

**Purpose: Tests the SQLite integration.**

**Operations validated:**

- Initialize a database.

- Insert records (Hello world, Temperature=24).

- Retrieve all records.

- Delete a record.

- Confirm deletion by retrieving again.

**Output: Shows inserted IDs, contents of the table, and deletion results.**

**c) Concurrency and Stress Test**

**Example:**

```
bash
make run TEST=test_client
```

**Purpose: Stress-tests the server by launching multiple threads that concurrently send CoAP GET requests.**

**Parameters:**

```
bash
./build/bin/test_client <server-ip> <port> <threads> <requests-per-thread>
```

**Behavior:**

- Each worker thread sends a series of CoAP requests.

- The program counts successful and failed responses.

**Output example:**

```
Thread 0: success=98 fail=2
Thread 1: success=95 fail=5
TOTAL: success=193 fail=7
```

**d) ESP32 Integration Scripts**

**Location: sh_files/**

**Purpose: Automates integration tests for the ESP32 simulator.**

**Examples:**

- Stress-test with simulated devices:

```
bash
make run TEST=test_client
./sh_files/run_stress.sh build/bin/test_client
```
##### Summary

- make test → compiles all test binaries.

- make run TEST=test_req001 → validates CoAP request/response flow.

- make run TEST=db_test → validates database operations.

- make run TEST=test_client → stress-tests server concurrency.

- make run TEST=esp32_cases → runs automated ESP32 integration scenarios.

**These tests ensure the correctness, reliability, and scalability of the CoAP server in a realistic IoT manufacturing environment.**

---

### Conclusions

 - Implementing CoAP from scratch strengthened the understanding of application-layer protocol design, especially when working with resource-constrained IoT environments.

- Using raw Berkeley sockets provided low-level control and insights into how transport mechanisms (UDP) interact with higher-layer protocols.

- Deploying the server in AWS EC2 introduced real-world challenges such as firewall rules, security groups, and cross-instance communication.

- The integration of ESP32-simulated sensors and a console-based client demonstrated the feasibility of managing IoT data end-to-end through CoAP.

- For these types of projects, the most recommended operating system is Linux, and we realized that most of the functions, libraries, and commands we needed most were part of Linux, not Windows. That's why we decided to use WSL.

- Having good practices when programming, both avoiding very compact code and the feat of "divide and conquer" helped us a lot when it came to obtaining good results from the protocol and implementation.

- This project not only met academic requirements but also provided practical skills applicable to industrial IoT and smart manufacturing systems.

---

# References

- Shelby, Z., Hartke, K., & Bormann, C. (2014). The Constrained Application Protocol (CoAP). RFC 7252, IETF. Retrieved from https://datatracker.ietf.org/doc/html/rfc7252

- Hall, B. B. (2025). Beej’s Guide to C Programming. Retrieved from https://beej.us/guide/bgc/

- Hall, B. B. (2025). Beej’s Guide to Network Programming. Retrieved from https://beej.us/guide/bgnet/

- GeeksforGeeks. (2025). TCP server-client implementation in C. Retrieved from https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/

- SQLite - C/C++ (2025)  Retrieved from https://www.tutorialspoint.com/sqlite/sqlite_c_cpp.htm

- CodeMagic LTD. (2019–2025). Wokwi: Online electronics simulator for embedded and IoT systems. Retrieved from https://wokwi.com/

- OpenAI. (2025). ChatGPT (versión GPT-5)

- A CoAP (RFC 7252) implementation in C Retrieved from https://github.com/obgm/libcoap

- Programación en lenguaje C en Linux.06 sockets – Estructuras serialización / deserialización [Video]. YouTube. https://www.youtube.com/watch?v=N7TSe1Q4ytg&list=PL19snTOMdnWupnb1vcTfjmp6BdNh_YxOp&index=6

 - DuarteCorporation Tutoriales - Tutorial de cómo usar Base de Datos en Lenguaje C usando SQLite [Video] Youtube. https://www.youtube.com/watch?v=oCUNa8RLgmA
