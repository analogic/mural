#ifndef Movement_h
#define Movement_h
#include "Arduino.h" 
#include "display.h"
#include "AccelStepper.h"
const auto printSpeedSteps = 500*2;
const auto moveSpeedSteps = 1500*2;
const long INFINITE_STEPS = 999999999;
const auto acceleration = 999999999; //essentially infinite, causing instant stop / start
const int stepsPerRotation = 200 * 16; // 1/16 microstepping
const auto diameter = 10;
const auto circumference = diameter * PI;
const auto bottomDistance = 67.4;
const auto midPulleyToWall = 41;
const auto safeYFraction = 0.2;
const auto safeXFraction = 0.2;

const auto LEFT_STEP_PIN = 27;
const auto LEFT_DIR_PIN = 26;
const auto LEFT_ENABLE_PIN = 25;

const auto RIGHT_STEP_PIN = 33;
const auto RIGHT_DIR_PIN = 32;
const auto RIGHT_ENABLE_PIN = 25;

const auto DRIVERS_UART_RX = 21;  // PDN_UART → RX2
const auto DRIVERS_UART_TX = 22;  // PDN_UART → TX2
const auto DRIVERS_R_SENSE = 0.22f;

const auto homedStepOffsetMM = 17;
const int homedStepsOffset = int((homedStepOffsetMM / circumference) * stepsPerRotation);



class Movement{
private:
int topDistance;
double minSafeY;
double minSafeXOffset;
double width;
volatile bool moving;
bool homed;
double X = -1;
double Y = -1;
bool startedHoming;
AccelStepper *leftMotor;
AccelStepper *rightMotor;
Display *display;
void setOrigin();
void initUartSteppers();


struct Lengths {
    int left;
    int right;
    Lengths(int left, int right) {
        this->left = left;
        this->right = right;
    }
    Lengths() {

    }
};

Lengths getBeltLengths(double x, double y);
public:
Movement(Display *display);
struct Point {
    double x;
    double y;
    Point(double x, double y) {
        this->x = x;
        this->y = y;
    }
    Point() {
        
    }
};

const int HOME_Y_OFFSET = 350;

static double distanceBetweenPoints(Point point1, Point point2) {
    return sqrt(pow(point2.x - point1.x, 2) + pow(point2.y - point1.y, 2));
}

bool isMoving();
bool hasStartedHoming();
double getWidth();
Point getCoordinates();
void setTopDistance(int distance);
void resumeTopDistance(int distance);
int getTopDistance();
void leftStepper(int dir);
void rightStepper(int dir);
int extendToHome();
void runSteppers();
float beginLinearTravel(double x, double y, int speed);
void extend100mm();
Point getHomeCoordinates();
void disableMotors();
};
#endif