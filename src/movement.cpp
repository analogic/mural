#include "movement.h"
#include "display.h"
#include <stdexcept>
#include <TMCStepper.h>

Movement::Movement(Display *display)
{
    this->display = display;

    initUartSteppers();
   
    leftMotor = new AccelStepper(AccelStepper::DRIVER, LEFT_STEP_PIN, LEFT_DIR_PIN);
    leftMotor->setEnablePin(LEFT_ENABLE_PIN);
    leftMotor->setMaxSpeed(moveSpeedSteps);
    
    leftMotor->disableOutputs();

    rightMotor = new AccelStepper(AccelStepper::DRIVER, RIGHT_STEP_PIN, RIGHT_DIR_PIN);
    rightMotor->setEnablePin(RIGHT_ENABLE_PIN);
    rightMotor->setMaxSpeed(moveSpeedSteps);
    rightMotor->setPinsInverted(true);
    rightMotor->disableOutputs();

    topDistance = -1;
   
    moving = false;
    homed = false;
    startedHoming = false;
};

void Movement::initUartSteppers()
{
    HardwareSerial tmcSerial(2);
    tmcSerial.begin(115200, SERIAL_8N1, DRIVERS_UART_RX, DRIVERS_UART_TX);
    while (!tmcSerial);

    Serial.println("TMC UART initialized");

    TMC2209Stepper drv1(&tmcSerial, DRIVERS_R_SENSE, 1); 
    TMC2209Stepper drv3(&tmcSerial, DRIVERS_R_SENSE, 3);

    auto version1 = drv1.version();
    if (version1 != 0x21) {
        Serial.println("Driver 1 version mismatch!");
        Serial.println("Expected version 0x21, got: " + String(version1, HEX));
        while (1) {};
    }
    auto version3 = drv3.version();
    if (version3 != 0x21) {
        Serial.println("Driver 3 version mismatch!");
        Serial.println("Expected version 0x21, got: " + String(version3, HEX));
        while (1) {};
    }

    drv1.toff(5);
    drv1.rms_current(400);
    drv1.mstep_reg_select(true);
    drv1.microsteps(16);
    drv1.push();

    drv3.toff(5);
    drv3.rms_current(400);
    drv3.mstep_reg_select(true);
    drv3.microsteps(16);
    drv3.push();
}

void Movement::setTopDistance(int distance) {
    Serial.printf("Top distance set to %s\n", String(distance));
    topDistance = distance;

    minSafeY = safeYFraction * topDistance;
    minSafeXOffset = safeXFraction * topDistance;
    width = topDistance - 2 * minSafeXOffset;
};

void Movement::resumeTopDistance(int distance) {
    setTopDistance(distance);
    homed = true;

    auto homeCoordinates = getHomeCoordinates();
    X = homeCoordinates.x;
    Y = homeCoordinates.y;

    auto lengths = getBeltLengths(homeCoordinates.x, homeCoordinates.y);
    leftMotor->setCurrentPosition(lengths.left);
    rightMotor->setCurrentPosition(lengths.right);

    moving = false;
}

void Movement::setOrigin()
{
    leftMotor->setCurrentPosition(homedStepsOffset);
    rightMotor->setCurrentPosition(homedStepsOffset);
    homed = true;
};

void Movement::leftStepper(int dir)
{
    if (dir > 0)
    {
        leftMotor->move(INFINITE_STEPS);
        leftMotor->setSpeed(printSpeedSteps);
    }
    else if (dir < 0)
    {
        leftMotor->move(-INFINITE_STEPS);
        leftMotor->setSpeed(printSpeedSteps);
    }
    else
    {
        leftMotor->setAcceleration(acceleration);
        leftMotor->stop();
    }

    moving = true;
};

void Movement::rightStepper(int dir)
{
    if (dir > 0)
    {
        rightMotor->move(INFINITE_STEPS);
        rightMotor->setSpeed(printSpeedSteps);
    }
    else if (dir < 0)
    {
        rightMotor->move(-INFINITE_STEPS);
        rightMotor->setSpeed(printSpeedSteps);
    }
    else
    {
        rightMotor->setAcceleration(acceleration);
        rightMotor->stop();
    }

    moving = true;
};

Movement::Point Movement::getHomeCoordinates() {
    if (topDistance == -1) {
        return Point(0, 0);
    }

    return Point(width / 2, HOME_Y_OFFSET);
}

int Movement::extendToHome()
{
    setOrigin();

    auto homeCoordinates = getHomeCoordinates();
    startedHoming = true;
    auto moveTime = beginLinearTravel(homeCoordinates.x, homeCoordinates.y, moveSpeedSteps);
    return int(ceil(moveTime));
};

void Movement::runSteppers()
{
    if (moving)
    {
        leftMotor->runSpeedToPosition();
        rightMotor->runSpeedToPosition();

        if (leftMotor->distanceToGo() == 0 && rightMotor->distanceToGo() == 0)
        {
            moving = false;
            //Serial.printf("Motion complete. Left steps: %ld, Right steps: %ld\n", leftMotor->currentPosition(), rightMotor->currentPosition());
        }
    }
};

Movement::Lengths Movement::getBeltLengths(double x, double y) {
    auto unsafeX = x + minSafeXOffset;
    auto unsafeY = y + minSafeY;

    // x deviation from the middle - the farther from the middle we go, the more extreme
    // the angle of Mural gets
    auto xDev = topDistance / 2 - unsafeX;
    
    // angle of tilt due to deviation from the middle is proportional to that deviation:
    // the closer we are to either edge the closer we get to the 90 degree tilt
    auto devAngle = (abs(xDev) / (topDistance / 2)) * (PI / 2);

    // we are rotating around the middle of bottomDistance
    auto halfBottom = bottomDistance / 2;

    // Flat coordinates of the left and right belt points before compensation for tilt
    auto flatLeftX = unsafeX - halfBottom;
    auto flatRightX = unsafeX + halfBottom;
    auto flatLeftY = unsafeY;
    auto flatRightY = unsafeY;

    // x compensation is 0 when angle is 0 (in the middle) and grows as the angle grows. The maximum theoretical compensation
    // is halfBottom if Mural is tilted 90 degrees, which it would never be in practice.
    // This is an absolute value of compensation - we'll change the sign later
    auto xComp = halfBottom - cos(devAngle) * halfBottom;

    auto yComp = sin(devAngle) * halfBottom;

    double leftX, leftY, rightX, rightY;

    if (xDev < 0) {
        // we're to the right of the middle axis, Mural is going to be tilting counterclockwise 
        leftX = flatLeftX + xComp;
        leftY = flatLeftY + yComp;
        rightX = flatRightX - xComp;
        rightY = flatRightY - yComp;  
    } else {
        // we're to the left of the middle axis, Mural is going to be tilting clockwise
        leftX = flatLeftX + xComp;
        leftY = flatLeftY - yComp;
        rightX = flatRightX - xComp;
        rightY = flatRightY + yComp;
    }

    // left and right leg distances flush to the wall
    auto leftLegFlat = sqrt(pow(leftX, 2) + pow(leftY, 2));
    auto rightLegFlat = sqrt(pow(topDistance - rightX, 2) + pow(rightY, 2));

    // left and right leg distances including the standoff length
    auto leftLeg = sqrt(pow(leftLegFlat, 2) + pow(midPulleyToWall, 2));
    auto rightLeg = sqrt(pow(rightLegFlat, 2) + pow(midPulleyToWall, 2));

    auto leftLegSteps = int((leftLeg / circumference) * stepsPerRotation);
    auto rightLegSteps = int((rightLeg / circumference) * stepsPerRotation);

    return Lengths(leftLegSteps, rightLegSteps);
}

float Movement::beginLinearTravel(double x, double y, int speed)
{
    X = x;
    Y = y;
    if (topDistance == -1 || !homed) {
        Serial.println("Not ready");
        throw std::invalid_argument("not ready");
    }

    if (x < 0 || (x - 1) > width)
    {
        Serial.println("Invalid x");
        throw std::invalid_argument("Invalid x");
    }

    if (y < 0)
    {
        Serial.println("Invalid y");
        throw std::invalid_argument("Invalid y");
    }

    auto lengths = getBeltLengths(x, y);
    auto leftLegSteps = lengths.left;
    auto rightLegSteps = lengths.right;

    auto deltaLeft = int(abs(abs(leftMotor->currentPosition()) - leftLegSteps));
    auto deltaRight = int(abs(abs(rightMotor->currentPosition()) - rightLegSteps));

    float leftSpeed, rightSpeed, moveTime;
    if (deltaLeft >= deltaRight)
    {
        leftSpeed = speed;
        moveTime = deltaLeft / leftSpeed;
        rightSpeed = deltaRight / moveTime;
    }
    else
    {
        rightSpeed = speed;
        moveTime = deltaRight / rightSpeed;
        leftSpeed = deltaLeft / moveTime;
    }

    //Serial.printf("Begin movement: X(%s) Y(%s) UnsafeX(%s) UnsafeY(%s) leftLeg(%s) rightLeg(%s) deltaLeft(%s) deltaRight(%s) leftSpeed(%s) rightSpeed(%s) \n", String(x), String(y), String(unsafeX), String(unsafeY), String(leftLeg), String(rightLeg), String(deltaLeft), String(deltaRight), String(leftSpeed), String(rightSpeed));
    leftMotor->moveTo(leftLegSteps);
    leftMotor->setSpeed(leftSpeed);
    
    rightMotor->moveTo(rightLegSteps);
    rightMotor->setSpeed(rightSpeed);

    //display->displayText(String(X) + ", " + String(Y));
    //delay(sleepAfterMove);

    moving = true;
    return moveTime;
};

double Movement::getWidth() {
    if (topDistance == -1) {
        throw std::invalid_argument("not ready");
    }
    return width;
}

Movement::Point Movement::getCoordinates() {
    if (X == -1 || Y == -1) {
        Serial.println("Not ready to get coordinates");
        throw std::invalid_argument("not ready");
    }

    if (moving) {
        Serial.println("Can't get coordinates while moving");
        throw std::invalid_argument("not ready");
    }
    return Movement::Point(X, Y);
}

void Movement::extend100mm() {
    auto steps = int((100 / circumference) * stepsPerRotation);
    
    leftMotor->move(steps);
    leftMotor->setSpeed(moveSpeedSteps);

    rightMotor->move(-steps);
    rightMotor->setSpeed(moveSpeedSteps);

    moving = true;
}

void Movement::disableMotors() {
    leftMotor->disableOutputs();
    rightMotor->disableOutputs();
}

bool Movement::isMoving() {
    return moving;
}

bool Movement::hasStartedHoming() {
    return startedHoming;
}

int Movement::getTopDistance() {
    return topDistance;
}
