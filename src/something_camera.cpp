#include "./something_camera.hpp"

void Camera::update(Seconds dt)
{
    pos += vel * dt;
}
