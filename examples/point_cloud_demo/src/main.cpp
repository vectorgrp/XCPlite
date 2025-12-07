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

namespace point_cloud {

// Calibration parameters
struct ParametersT {

    double boundary;     // boundary_ box size in m
    double max_radius;   // Point radius in m
    double max_velocity; // Maximum point velocity in m/s
    double ttl;          // Time to live in s
    uint16_t spawn_rate; // Points to spawn per second
    double gravity;      // Gravity in m/s²
    uint32_t delay_us;   // Delay per simulation step in microseconds
};

// Default parameter values
const ParametersT kParameters = {

    .boundary = 10.0,     // boundary_ in m
    .max_radius = 0.5,    // max_radius in m
    .max_velocity = 20.0, // max_velocity in m/s
    .ttl = 10.0,          // ttl in seconds
    .gravity = 9.81,      // gravity in m/s²
    .delay_us = 20000     // delay_us in microseconds
};

//-----------------------------------------------------------------------------------------------------

struct Point {
    float x;   // x coord in m
    float y;   // y coord in m
    float z;   // z coord in m
    float v_x; // x velocity in m/s
    float v_y; // y velocity in m/s
    float v_z; // z velocity in m/s
    float r;   // radius in m

    void clear() {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
        v_x = 0.0f;
        v_y = 0.0f;
        v_z = 0.0f;
        r = 0.0f;
    }

    void move(double dx, double dy, double dz) {
        x += (float)dx;
        y += (float)dy;
        z += (float)dz;
    }

    void print() const {
        std::cout << "pos=(" << x << ", " << y << ", " << z << "), "
                  << "vel=(" << v_x << ", " << v_y << ", " << v_z << ")" << " r=" << r << std::endl;
    }
};

template <uint16_t N> class PointCloud {

  private:
    uint32_t step_counter_;
    uint16_t last_time;
    uint16_t count_;
    double boundary_;
    std::array<Point, N> points_;
    xcplib::CalSeg<ParametersT> params_;

  private:
    void createA2L() {

        if (A2lOnce()) {

            // Register the calibration segment description as a typedef and an instance in the A2L file
            A2lTypedefBegin(ParametersT, &kParameters, "Typedef for ParametersT");
            A2lTypedefParameterComponent(boundary, "boundary_ box size in meters", "m", 0.1, 1000.0);
            A2lTypedefParameterComponent(gravity, "Gravity in meters per second squared", "m/s²", 0.1, 1000.0);
            A2lTypedefParameterComponent(max_radius, "Maximum point radius in meters", "m", 0.01, 10.0);
            A2lTypedefParameterComponent(max_velocity, "Maximum point velocity in meters per second", "m/s", 0.1, 1000.0);
            A2lTypedefParameterComponent(ttl, "Time to live for points in seconds", "s", 0.1, 1000.0);
            A2lTypedefParameterComponent(spawn_rate, "Points to spawn per second", "1/s", 1, 1000);
            A2lTypedefParameterComponent(delay_us, "Delay per simulation step in microseconds", "us", 0, 1000000);
            A2lTypedefEnd();
            params_.CreateA2lTypedefInstance("ParametersT", "Random number generator parameters");

            // Register Point typedef first - this must be done before PointCloud typedef
            // because PointCloud contains an array of Points

            A2lTypedefBegin(Point, NULL, "Typedef for Point");
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
            A2lTypedefMeasurementComponent(step_counter_, "Global step counter");
            A2lTypedefComponent(points_, Point, N); // Array of N Points
            A2lTypedefEnd();
        }
    }

    // Add a new point to the cloud
    // Create a point at (0,0,0) with random velocity (v_x, v_y, v_z) between -10.0 and +10.0 m/s
    void add_point() {
        if (count_ < N) {
            auto params = params_.lock();
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
            std::cout << step_counter_ << ": point " << count_ << " spawned (count=" << count_ << ")" << std::endl;
            count_++;
        }
    };

    // Remove a point from the cloud by index
    void remove_point(uint16_t index) {
        if (index < count_) {
            points_[index] = points_[count_ - 1];
            count_--;
            points_[count_].clear();
        }
    };

    // Remove points that are out of the boundary_
    void remove_out_boundary_points() {
        for (uint16_t i = 0; i < count_;) {
            if (std::abs(points_[i].x) > boundary_ || std::abs(points_[i].y) > boundary_ || std::abs(points_[i].z) > boundary_) {
                remove_point(i);
                std::cout << step_counter_ << ": point " << i << " removed (count=" << count_ << ")" << std::endl;
                continue; // skip i increment
            }
            i++;
        }
    }

    // Calculate Euclidean distance between two points
    double calc_distance(const Point &p1, const Point &p2) const { return sqrt(pow(p2.x - p1.x, 2) + pow(p2.y - p1.y, 2) + pow(p2.z - p1.z, 2)); }

    // Check if two points are colliding
    bool check_collision(const Point &point1, const Point &point2) const { return calc_distance(point1, point2) < point1.r + point2.r; }

    // Check for boundary_ collisions and respond
    void check_boundary_collisions() {

        for (uint16_t i = 0; i < count_; i++) {
            if (points_[i].x < -boundary_ || points_[i].x > boundary_) {
                points_[i].v_x = -points_[i].v_x;
            }
            if (points_[i].y < -boundary_ || points_[i].y > boundary_) {
                points_[i].v_y = -points_[i].v_y;
            }
            if (points_[i].z < -boundary_ || points_[i].z > boundary_) {
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
        auto params = params_.lock();
        double ttl = params->ttl;
        double max_radius = params->max_radius;
        double delta_r = max_radius / (ttl * 1e6 / (double)params->delay_us);

        for (uint16_t i = 0; i < count_;) {
            if (points_[i].r > 0.0) {
                points_[i].r -= (float)delta_r;
                if (points_[i].r <= 0.0f) {
                    remove_point(i);
                    std::cout << step_counter_ << ": point " << i << " removed (count=" << count_ << ")" << std::endl;
                    continue; // skip i increment
                }
            }
            i++;
        }
    }

  public:
    PointCloud() : step_counter_(0), last_time(clockGetUs()), count_(0), boundary_(kParameters.boundary), params_("Parameters", &kParameters) {
        for (uint16_t i = 0; i < N; i++) {
            points_[i].clear();
        }
        createA2L();
        std::cout << "PointCloud<" << (unsigned int)N << "> instance created" << std::endl;
    }

    ~PointCloud() = default;

    // Getters
    uint16_t get_count() const { return count_; }
    const Point &get_point(uint16_t index) const { return points_[index]; }

    // Perform a simulation step: move points, check for collisions
    void step() {

        uint64_t time = clockGetUs();

        // Cycle timer
        if (time - last_time < 1000000 / params_.lock()->delay_us) {
            return; // not time yet
        }
        last_time = time;

        // Check parameter changes of boundary_ and remove out-of-boundary points
        auto params = params_.lock();
        if (boundary_ != params->boundary) {
            std::cout << "boundary_ changed from " << boundary_ << " to " << params->boundary << std::endl;
            remove_out_boundary_points();
        }
        boundary_ = params->boundary;

        // Add a new point to the point if there is capacity
        add_point();

        // Move
        double delta_t = (double)params_.lock()->delay_us / 1e6; // s
        for (uint16_t i = 0; i < count_; i++) {
            points_[i].move((double)points_[i].v_x * delta_t, (double)points_[i].v_y * delta_t, (double)points_[i].v_z * delta_t);
        }

        // Apply gravity to z velocity
        for (uint16_t i = 0; i < count_; i++) {
            points_[i].v_z -= static_cast<float>(params->gravity * delta_t);
        }

        // Check collisions
        check_boundary_collisions();
        check_point_collisions();

        // Lifetime
        check_lifetime();

        // XCP measurement event
        DaqEventVar(step,                                                        //
                    A2L_MEAS(count_, "Current point count"),                     //
                    A2L_MEAS(step_counter_, "Step counter"),                     //
                    A2L_MEAS_INST_ARRAY(points_, "Point", "Points in the cloud") // Array of Point instances

        );
    }
};

} // namespace point_cloud

//-----------------------------------------------------------------------------------------------------

// Signal handler for graceful exit on Ctrl+C
std::atomic<bool> gRun{true};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        gRun = false;
    }
}

int main() {

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "\nXCP on Ethernet demo - simple C++ example\n" << std::endl;

    // Initialize random seed
    srand(static_cast<unsigned int>(time(nullptr)));

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

    // Create a PointCloud instance with max N=256 points
    point_cloud::PointCloud<256> point_cloud;

    // Main loop
    std::cout << "\nStarting main loop... (Press Ctrl+C to exit)" << std::endl;
    while (gRun) {

        // Calculate a simulation step
        point_cloud.step();
        sleepUs(10); // Yield

        A2lFinalize(); // @@@@ TEST: Manually finalize the A2L file after first step
    }

    // Cleanup
    std::cout << "\nExiting ..." << std::endl;
    XcpDisconnect();
    XcpEthServerShutdown();

    return 0;
}
