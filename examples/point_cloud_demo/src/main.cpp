// point_cloud_demo - simple xcplib C++ example

#include <algorithm> // for std::min
#include <array>     // for std::array
#include <atomic>    // for std::atomic
#include <cmath>     // for sqrt, pow, cos, sin
#include <csignal>   // for signal handling
#include <cstdint>   // for uintxx_t
#include <ctime>     // for time
#include <iostream>  // for std::cout
#include <optional>  // for std::optional

#include <a2l.hpp>    // for xcplib A2l generation application programming interface
#include <xcplib.hpp> // for xcplib application programming interface

//-----------------------------------------------------------------------------------------------------
// XCP parameters

constexpr const char OPTION_PROJECT_NAME[] = "point_cloud_demo";
constexpr const char OPTION_PROJECT_VERSION[] = __TIME__;
constexpr bool OPTION_USE_TCP = true;
constexpr uint16_t OPTION_SERVER_PORT = 5555;
constexpr size_t OPTION_QUEUE_SIZE = 1024 * 64;
constexpr int OPTION_LOG_LEVEL = 3;
constexpr uint8_t OPTION_SERVER_ADDR[] = {0, 0, 0, 0};

//-----------------------------------------------------------------------------------------------------

// Calibration parameters for the random number generator
struct ParametersT {

    double boundary;     // Boundary box size in m
    double max_radius;   // Point radius in m
    double max_velocity; // Maximum point velocity in m/s
    double ttl;          // Time to live in s
    uint16_t spawn_rate; // Points to spawn per second
    double gravity;      // Gravity in m/s²
    uint32_t delay_us;   // Delay per simulation step in microseconds
};

// Default parameter values
const ParametersT kParameters = {

    .boundary = 10.0,      // boundary in m
    .max_radius = 0.1,     // max_radius in m
    .max_velocity = 100.0, // max_velocity in m/s
    .ttl = 10.0,           // ttl in seconds
    .gravity = 9.81,       // gravity in m/s²
    .delay_us = 20000      // delay_us in microseconds
};

// A global calibration parameter segment handle for struct 'ParametersT'
// Initialized in main(), after XCP initialization
std::optional<xcplib::CalSeg<ParametersT>> gCalSeg;

//-----------------------------------------------------------------------------------------------------

namespace point_cloud {

struct Point {
    float x;   // x coord in m
    float y;   // y coord in m
    float z;   // z coord in m
    float v_x; // x velocity in m/s
    float v_y; // y velocity in m/s
    float v_z; // z velocity in m/s
    float r;   // radius in m
};

template <uint16_t N> class PointCloud {

  private:
    uint16_t count_;
    std::array<Point, N> points_;

  public:
    // Invalidate point
    void clear_point(uint16_t index) {
        Point &point = points_[index];
        point.x = 0.0f;
        point.y = 0.0f;
        point.z = 0.0f;
        point.v_x = 0.0f;
        point.v_y = 0.0f;
        point.v_z = 0.0f;
        point.r = 0.0f;
    }

    // Add a new point to the cloud
    // Create a point at (0,0,0) with random velocity (v_x, v_y, v_z) between -10.0 and +10.0 m/s
    void add_point() {
        if (count_ < N) {
            auto params = gCalSeg->lock();
            float max_vel = static_cast<float>(params->max_velocity);
            float max_rad = static_cast<float>(params->max_radius);
            Point p = {.x = 0.0f,
                       .y = 0.0f,
                       .z = 0.0f,
                       .v_x = static_cast<float>(rand()) / RAND_MAX * 2.0f * max_vel - max_vel,
                       .v_y = static_cast<float>(rand()) / RAND_MAX * 2.0f * max_vel - max_vel,
                       .v_z = static_cast<float>(rand()) / RAND_MAX * 2.0f * max_vel - max_vel,
                       .r = static_cast<float>(rand()) / RAND_MAX * max_rad};
            points_[count_] = p;
            count_++;
        }
    };

    // Remove a point from the cloud by index
    void remove_point(uint16_t index) {
        if (index < count_) {
            points_[index] = points_[count_ - 1];
            count_--;
            clear_point(count_);
        }
    };

    // Move a point by (dx, dy, dz)
    void move_point(Point &point, double dx, double dy, double dz) {
        point.x += (float)dx;
        point.y += (float)dy;
        point.z += (float)dz;
    }

    // Calculate Euclidean distance between two points
    double calc_distance(const Point &p1, const Point &p2) const { return sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2) + pow(p2.z - p1.z, 2)); }

    // Check if two points are colliding
    bool check_collision(const Point &point1, const Point &point2) const { return calc_distance(point1, point2) < point1.r + point2.r; }

    // Check for boundary collisions and respond
    void check_boundary_collisions() {

        double boundary = gCalSeg->lock()->boundary;

        for (uint16_t i = 0; i < count_; i++) {
            if (points_[i].x < -boundary || points_[i].x > boundary) {
                points_[i].v_x = -points_[i].v_x;
            }
            if (points_[i].y < -boundary || points_[i].y > boundary) {
                points_[i].v_y = -points_[i].v_y;
            }
            if (points_[i].z < -boundary || points_[i].z > boundary) {
                points_[i].v_z = -points_[i].v_z;
            }
        }
    }

    // Check for collisions between points and respond
    void check_point_collisions() {
        for (uint16_t i = 0; i < count_; i++) {
            for (uint16_t j = i + 1; j < count_; j++) {
                if (check_collision(points_[i], points_[j])) {
                    // Simple elastic collision response
                    std::swap(points_[i].v_x, points_[j].v_x);
                    std::swap(points_[i].v_y, points_[j].v_y);
                    std::swap(points_[i].v_z, points_[j].v_z);
                }
            }
        }
    }

    // Check and remove points that have exceeded their lifetime (shrinking to zero radius)
    void check_lifetime() {
        auto params = gCalSeg->lock();
        double ttl = params->ttl;
        double delta_r = (double)params->delay_us / 1e6; // s

        for (uint16_t i = 0; i < count_; i++) {
            if (points_[i].r > 0.0) {
                points_[i].r -= (float)delta_r;
                if (points_[i].r <= 0.0f) {
                    remove_point(i);
                    i--; // Check the swapped point in the next iteration
                }
            }
        }
    }

    // Perform a simulation step: move points, check for collisions
    void step() {

        double delta_t = (double)gCalSeg->lock()->delay_us / 1e6; // s

        for (uint16_t i = 0; i < count_; i++) {
            move_point(points_[i], (double)points_[i].v_x * delta_t, (double)points_[i].v_y * delta_t, (double)points_[i].v_z * delta_t);
        }

        check_boundary_collisions();
        check_point_collisions();
        check_lifetime();

        DaqEventVar(step,                                                        //
                    A2L_MEAS(count_, "Current point count"),                     //
                    A2L_MEAS_INST_ARRAY(points_, "Point", "Points in the cloud") //

        );
    }

    PointCloud();
    ~PointCloud() = default;

    // Getters
    uint16_t get_count() const { return count_; }
    const Point &get_point(uint16_t index) const { return points_[index]; }

    // Print basic statistics about the point cloud
    void print_stats() const {
        std::cout << "PointCloud: " << count_ << "/" << N << " points" << std::endl;
        for (uint16_t i = 0; i < std::min(count_, static_cast<uint16_t>(5)); i++) {
            std::cout << "  Point[" << i << "]: pos=(" << points_[i].x << ", " << points_[i].y << ", " << points_[i].z << "), "
                      << "vel=(" << points_[i].v_x << ", " << points_[i].v_y << ", " << points_[i].v_z << ")" << std::endl;
        }
        if (count_ > 5) {
            std::cout << "  ... (" << (count_ - 5) << " more points)" << std::endl;
        }
    }
};

// PointCloud constructor with A2L typedef registration
template <uint16_t N> PointCloud<N>::PointCloud() : count_(0) {

    // Create a global calibration segment wrapper for the struct 'ParametersT' and use its default values in constant 'kParameters'
    // This calibration segment has a working page (RAM) and a reference page (FLASH), it creates a MEMORY_SEGMENT in the A2L file
    // It provides safe (thread safe against XCP modifications), lock-free and consistent access to the calibration parameters
    // It supports XCP/ECU independent page switching, checksum calculation and reinitialization (copy reference page to working page)
    gCalSeg.emplace("Parameters", &kParameters);

    if (A2lOnce()) {

        // Register the calibration segment description as a typedef and an instance in the A2L file
        A2lTypedefBegin(ParametersT, &kParameters, "Typedef for ParametersT");
        A2lTypedefParameterComponent(boundary, "Boundary box size in meters", "m", 0.1, 1000.0);
        A2lTypedefParameterComponent(gravity, "Gravity in meters per second squared", "m/s²", 0.1, 1000.0);
        A2lTypedefParameterComponent(max_radius, "Maximum point radius in meters", "m", 0.01, 10.0);
        A2lTypedefParameterComponent(max_velocity, "Maximum point velocity in meters per second", "m/s", 0.1, 1000.0);
        A2lTypedefParameterComponent(ttl, "Time to live for points in seconds", "s", 0.1, 1000.0);
        A2lTypedefParameterComponent(spawn_rate, "Points to spawn per second", "1/s", 1, 1000);
        A2lTypedefParameterComponent(delay_us, "Delay per simulation step in microseconds", "us", 0, 1000000);
        A2lTypedefEnd();
        gCalSeg->CreateA2lTypedefInstance("ParametersT", "Random number generator parameters");

        // Register Point typedef first - this must be done before PointCloud typedef
        // because PointCloud contains an array of Points
        Point dummy_point;
        A2lTypedefBegin(Point, &dummy_point, "Typedef for Point");
        A2lTypedefMeasurementComponent(x, "X coordinate of the point");
        A2lTypedefMeasurementComponent(y, "Y coordinate of the point");
        A2lTypedefMeasurementComponent(z, "Z coordinate of the point");
        A2lTypedefMeasurementComponent(r, "Radius of the point");
        A2lTypedefMeasurementComponent(v_x, "X velocity of the point");
        A2lTypedefMeasurementComponent(v_y, "Y velocity of the point");
        A2lTypedefMeasurementComponent(v_z, "Z velocity of the point");
        A2lTypedefEnd();

        // Register PointCloud typedef - now Point typedef exists
        A2lTypedefBegin(PointCloud, this, "Typedef for PointCloud");
        A2lTypedefMeasurementComponent(count_, "Current number of points in the cloud");
        A2lTypedefComponent(points_, Point, N); // Array of N Points
        A2lTypedefEnd();
    }

    std::cout << "PointCloud<" << (unsigned int)N << "> instance created" << std::endl;
}

} // namespace point_cloud

//-----------------------------------------------------------------------------------------------------

// Signal handler for graceful exit on Ctrl+C
std::atomic<bool> gRun{true};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        gRun = false;
    }
}

// Define a global variable to be measured later in the main loop
uint16_t global_counter{0};

int main() {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nXCP on Ethernet demo - simple C++ example\n" << std::endl;

    // Set log level (1-error, 2-warning, 3-info, 4-show XCP commands)
    XcpSetLogLevel(OPTION_LOG_LEVEL);

    // Initialize the XCP singleton and activate XCP
    XcpInit(OPTION_PROJECT_NAME, OPTION_PROJECT_VERSION /* EPK version*/, true /* activate */);

    // Initialize the XCP Server
    if (!XcpEthServerInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, OPTION_QUEUE_SIZE)) {
        std::cerr << "Failed to initialize XCP server" << std::endl;
        return 1;
    }

    // Enable runtime A2L generation for data declaration as code
    // The A2L file will be created when the XCP tool connects and, if it does not already exist on local file system and the version did not change
    if (!A2lInit(OPTION_SERVER_ADDR, OPTION_SERVER_PORT, OPTION_USE_TCP, A2L_MODE_WRITE_ONCE | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS)) {
        std::cerr << "Failed to initialize A2L generator" << std::endl;
        return 1;
    }

    // Create a simple arithmetic local variable
    uint16_t counter{0};

    // Initialize random seed for point generation
    srand(static_cast<unsigned int>(time(nullptr)));

    // Create PointCloud instance with max N=64 points
    point_cloud::PointCloud<64> point_cloud;

    // Main loop
    std::cout << "\nStarting main loop... (Press Ctrl+C to exit)" << std::endl;
    uint32_t step_counter = 0;
    uint64_t last_time = clockGetUs();
    while (gRun) {

        uint64_t time = clockGetUs();
        global_counter++;

        std::cout << "\n--- Step " << global_counter << " (t=" << time << "us)" << std::endl;

        // Add a new point to the point count according to spawn rate
        if (time - last_time >= 1000000 / gCalSeg->lock()->spawn_rate) {
            last_time = time;

            point_cloud.add_point();

            // Print stats
            point_cloud.print_stats();
        }

        std::cout << "\n--- Step " << global_counter << " (t=" << time << "us)" << std::endl;

        // Calculate a simulation step
        // point_cloud.step();

        // Trigger data acquisition event "mainloop", once register event, global and local variables, and heap instance measurements
        DaqEventVar(mainloop,                                           //
                    A2L_MEAS(global_counter, "Global counter variable") //

        );

        // Sleep, use delay from calibration parameters
        uint32_t delay_us = gCalSeg->lock()->delay_us;
        sleepUs(delay_us);

        if (step_counter == 1) {
            A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file after first step
        }
    }

    // Cleanup
    std::cout << "\nExiting ..." << std::endl;
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
