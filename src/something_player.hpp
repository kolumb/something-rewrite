#ifndef SOMETHING_PLAYER_HPP_
#define SOMETHING_PLAYER_HPP_

#include "./something_geo.hpp"
#include "./something_renderer.hpp"

enum class Direction {
    Right = 0,
    Left,
};

struct Game;

struct Player {
    V2<float> pos;
    V2<float> vel;
    Direction direction;

    void render(const Game *game, Renderer *renderer) const;
    void update(Game *game, Seconds dt);

    void jump();
    void move(Direction direction);
    void stop();
};

#endif // SOMETHING_PLAYER_HPP_
