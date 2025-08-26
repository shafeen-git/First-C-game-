#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#define MAX_ENEMIES   100
#define MAX_BULLETS   500
#define MAX_OBSTACLES 4
#define NUM_PINS      10

// Bowling sizes/speeds
#define BALL_RADIUS   15
#define PIN_RADIUS    20
#define LANE_LEFT     140
#define LANE_RIGHT    660

typedef enum {
    OPENING_SCENE,
    GAMEPLAY,
    MINI_GAME,
    CLOSING_SCENE
} GameState;

typedef enum {
    DIFFICULTY_EASY,
    DIFFICULTY_MEDIUM,
    DIFFICULTY_HARD
} Difficulty;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    bool active;
} Bullet;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    float speed;
    bool active;
} Enemy;

typedef struct {
    Rectangle rect;
    bool active;
} Obstacle;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    float rotation;
    bool fallen;
    bool animating;
} Pin;

// ------------ Globals ------------
Bullet   bullets[MAX_BULLETS];
Enemy    enemies[MAX_ENEMIES];
Obstacle obstacles[MAX_OBSTACLES];
Pin      pins[NUM_PINS];

Vector2  playerPos = {400, 300};
float    playerSpeed = 200.0f;
int      score = 0;
bool     gameOver = false;
bool     secondChanceUsed = false;

Font     emojiFont;
Texture2D pokeballTex;
Texture2D pikachuTex;
Texture2D logo;
Texture2D balhTex;
Texture2D obstacleTex;

// Elixir buff system
Texture2D elixirTex;
bool     elixirAvailable = false; // Elixir is on the map
Vector2  elixirPos = {0};
bool     elixirReady = false;     // Player has collected elixir
float    elixirSpawnTimer = 0.0f; // Time since last spawn attempt
float    elixirDurationTimer = 0.0f; // Time elixir has been on map
float    elixirSpawnInterval = 0.0f; // Set by difficulty (5s Medium, 7s Hard)
const float ELIXIR_DURATION = 8.0f;   // Elixir lasts 8 seconds

// Bowling state
Vector2  ballPos;
float    t = 0.0f;
float    throwAngle = 0.0f;
const float maxAngle = PI / 6;
Vector2  ellipseCenter;
float    a = 100.0f;
float    b = 500.0f;
float    baseSpeed = 0.02f;
float    ballSpeed = 0.0f;
bool     ballLaunched = false;
float    power = 0.0f;
float    maxPower = 1.0f;
bool     charging = false;
bool     strikeMode = false;
bool     luckyStrike = false;

// Bowling assets
Sound    hitSound;
Texture2D bowlingBg;

// ------------ Helpers ------------
static void ResetElixirState(void) {
    elixirAvailable = false;
    elixirReady = false;
    elixirSpawnTimer = 0.0f;
    elixirDurationTimer = 0.0f;
}

static void ResetGame(Difficulty difficulty) {
    playerPos = (Vector2){400, 300};
    score = 0;
    gameOver = false;
    for (int i = 0; i < MAX_ENEMIES;  i++) enemies[i].active = false;
    for (int i = 0; i < MAX_BULLETS;  i++) bullets[i].active = false;
    for (int i = 0; i < MAX_OBSTACLES; i++) obstacles[i].active = false;
    ResetElixirState();
}

static void SpawnEnemy(Difficulty difficulty) {
    int screenWidth  = GetScreenWidth();
    int screenHeight = GetScreenHeight();

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            int side = GetRandomValue(0, 3);
            Vector2 pos;
            switch (side) {
                case 0: pos = (Vector2){0, GetRandomValue(0, screenHeight)}; break;
                case 1: pos = (Vector2){screenWidth, GetRandomValue(0, screenHeight)}; break;
                case 2: pos = (Vector2){GetRandomValue(0, screenWidth), 0}; break;
                default: pos = (Vector2){GetRandomValue(0, screenWidth), screenHeight}; break;
            }

            float baseSpeed = 50.0f;
            float speed = baseSpeed;
            if (difficulty == DIFFICULTY_MEDIUM) speed = baseSpeed * 1.7f;
            if (difficulty == DIFFICULTY_HARD)   speed = baseSpeed * 2.0f;

            enemies[i].position = pos;
            enemies[i].speed = speed;
            enemies[i].velocity = Vector2Scale(Vector2Normalize(Vector2Subtract(playerPos, pos)), speed);
            enemies[i].active = true;
            break;
        }
    }
}

static void SpawnObstacles(void) {
    Rectangle playerRect = {
        playerPos.x - pikachuTex.width/2.0f,
        playerPos.y - pikachuTex.height/2.0f,
        (float)pikachuTex.width,
        (float)pikachuTex.height
    };

    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            float w = (float)obstacleTex.width;
            float h = (float)obstacleTex.height;
            float x = (float)GetRandomValue(100, GetScreenWidth()  - (int)w);
            float y = (float)GetRandomValue(100, GetScreenHeight() - (int)h);
            Rectangle obsRect = (Rectangle){x, y, w, h};

            while (CheckCollisionRecs(playerRect, obsRect)) {
                x = (float)GetRandomValue(100, GetScreenWidth()  - (int)w);
                y = (float)GetRandomValue(100, GetScreenHeight() - (int)h);
                obsRect = (Rectangle){x, y, w, h};
            }

            obstacles[i].rect = obsRect;
            obstacles[i].active = true;
        }
    }
}

static void ShootBullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].position = playerPos;
            bullets[i].velocity = (Vector2){0, -400};
            bullets[i].active = true;
            break;
        }
    }
}

static void LayoutPins(void) {
    const float cx = GetScreenWidth() / 2.0f;
    const float topY = 120.0f;
    const float spacing = 35.0f;

    int idx = 0;
    for (int row = 0; row < 4; row++) {
        for (int i = 0; i <= row; i++) {
            if (idx < NUM_PINS) {
                pins[idx].position.x = cx + (i - row / 2.0f) * spacing;
                pins[idx].position.y = topY + row * spacing;
                pins[idx].fallen = false;
                pins[idx].animating = false;
                pins[idx].velocity = (Vector2){0, 0};
                pins[idx].rotation = 0;
                idx++;
            }
        }
    }
}

static void ResetBowling(void) {
    ballPos = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight() - 80.0f };
    t = 0.0f;
    ballSpeed = 0.0f;
    throwAngle = 0.0f;
    ballLaunched = false;
    power = 0.0f;
    charging = false;
    strikeMode = false;
    luckyStrike = false;
    LayoutPins();
}

// ------------ Main ------------
int main(void) {
    const int screenWidth = 800;
    const int screenHeight = 600;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Capture or Escape");
    InitAudioDevice();
    SetTargetFPS(60);

    // --- Load assets ---
    if (!FileExists("resources/logo.png")) TraceLog(LOG_WARNING, "logo.png missing!");
    logo = LoadTexture("resources/logo.png");

    if (!FileExists("resources/emoji_font.ttf")) TraceLog(LOG_WARNING, "emoji_font.ttf missing! Using default.");
    emojiFont = LoadFont("resources/emoji_font.ttf");
    if (emojiFont.texture.id == 0) emojiFont = GetFontDefault();

    if (!FileExists("resources/pikachu.png")) TraceLog(LOG_WARNING, "pikachu.png missing!");
    pikachuTex = LoadTexture("resources/pikachu.png");

    if (!FileExists("resources/pokeball.png")) TraceLog(LOG_WARNING, "pokeball.png missing!");
    pokeballTex = LoadTexture("resources/pokeball.png");

    if (!FileExists("resources/balh.png")) TraceLog(LOG_WARNING, "balh.png missing!");
    balhTex = LoadTexture("resources/balh.png");

    if (!FileExists("resources/Rock.png")) TraceLog(LOG_WARNING, "Rock.png missing!");
    Image rockImg = LoadImage("resources/Rock.png");
    ImageResize(&rockImg, rockImg.width / 3, rockImg.height / 3);
    obstacleTex = LoadTextureFromImage(rockImg);
    UnloadImage(rockImg);

    if (!FileExists("resources/elixir.png")) TraceLog(LOG_WARNING, "elixir.png missing! A fallback circle will be drawn.");
    elixirTex = LoadTexture("resources/elixir.png");

    hitSound = LoadSound("resources/strike.wav");
    bowlingBg = LoadTexture("resources/background.png");

    GameState gameState = OPENING_SCENE;
    Difficulty selectedDifficulty = DIFFICULTY_MEDIUM;

    Rectangle easyBtn = { screenWidth/2 - 100, 300, 200, 50 };
    Rectangle mediumBtn = { screenWidth/2 - 100, 370, 200, 50 };
    Rectangle hardBtn = { screenWidth/2 - 100, 440, 200, 50 };
    Rectangle startBtn = { screenWidth/2 - 100, 510, 200, 50 };

    float gameOverScale = 0.1f;
    float scaleSpeed = 1.5f;
    bool animationComplete = false;
    float enemySpawnTimer = 0.0f;

    ResetBowling();
    ResetElixirState();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // ---------------- UPDATE ----------------
        switch (gameState) {
            case OPENING_SCENE: {
                if (CheckCollisionPointRec(GetMousePosition(), easyBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    selectedDifficulty = DIFFICULTY_EASY;
                if (CheckCollisionPointRec(GetMousePosition(), mediumBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    selectedDifficulty = DIFFICULTY_MEDIUM;
                if (CheckCollisionPointRec(GetMousePosition(), hardBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    selectedDifficulty = DIFFICULTY_HARD;
                if (CheckCollisionPointRec(GetMousePosition(), startBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    ResetGame(selectedDifficulty);
                    if (selectedDifficulty == DIFFICULTY_HARD) SpawnObstacles();
                    elixirSpawnInterval = (selectedDifficulty == DIFFICULTY_MEDIUM) ? 5.0f : (selectedDifficulty == DIFFICULTY_HARD) ? 7.0f : 0.0f;
                    secondChanceUsed = false;
                    gameState = GAMEPLAY;
                }
            } break;

            case GAMEPLAY: {
                if (!gameOver) {
                    float delta_x = 0.0f;
                    if (IsKeyDown(KEY_LEFT))  delta_x -= playerSpeed * dt;
                    if (IsKeyDown(KEY_RIGHT)) delta_x += playerSpeed * dt;
                    float delta_y = 0.0f;
                    if (IsKeyDown(KEY_UP))    delta_y -= playerSpeed * dt;
                    if (IsKeyDown(KEY_DOWN))  delta_y += playerSpeed * dt;
                    playerPos.x += delta_x;
                    playerPos.y += delta_y;

                    if (IsKeyPressed(KEY_SPACE)) ShootBullet();

                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (bullets[i].active) {
                            bullets[i].position.y += bullets[i].velocity.y * dt;
                            if (bullets[i].position.y < 0) bullets[i].active = false;
                        }
                    }

                    float spawnInterval = (selectedDifficulty == DIFFICULTY_EASY) ? 1.5f :
                                          (selectedDifficulty == DIFFICULTY_MEDIUM) ? 1.0f : 0.7f;
                    enemySpawnTimer += dt;
                    if (enemySpawnTimer > spawnInterval - (score * 0.01f)) {
                        SpawnEnemy(selectedDifficulty);
                        enemySpawnTimer = 0;
                    }

                    // Elixir spawn logic
                    if (selectedDifficulty != DIFFICULTY_EASY && !elixirAvailable && !elixirReady && elixirSpawnInterval > 0.0f) {
                        elixirSpawnTimer += dt;
                        if (elixirSpawnTimer >= elixirSpawnInterval) {
                            elixirSpawnTimer = 0.0f;
                            float margin = 50.0f; // Adjusted for 100x100 elixir
                            elixirPos.x = GetRandomValue((int)margin, GetScreenWidth() - (int)margin);
                            elixirPos.y = GetRandomValue((int)margin, GetScreenHeight() - (int)margin);
                            elixirAvailable = true;
                            elixirDurationTimer = 0.0f;
                        }
                    }

                    // Elixir duration and collection
                    if (elixirAvailable) {
                        elixirDurationTimer += dt;
                        if (elixirDurationTimer >= ELIXIR_DURATION) {
                            elixirAvailable = false;
                            elixirDurationTimer = 0.0f;
                        } else {
                            float pickupRadius = 50.0f; // Match 100x100 visual size
                            if (CheckCollisionCircles(playerPos, 20.0f, elixirPos, pickupRadius)) {
                                elixirAvailable = false;
                                elixirReady = true;
                                elixirDurationTimer = 0.0f;
                            }
                        }
                    }

                    // Use elixir to destroy all enemies
                    if (elixirReady && IsKeyPressed(KEY_S)) {
                        for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
                        elixirReady = false;
                    }

                    for (int i = 0; i < MAX_ENEMIES; i++) {
                        if (enemies[i].active) {
                            Vector2 direction = Vector2Subtract(playerPos, enemies[i].position);
                            if (Vector2Length(direction) > 0.0f)
                                enemies[i].velocity = Vector2Scale(Vector2Normalize(direction), enemies[i].speed);
                            enemies[i].position = Vector2Add(enemies[i].position, Vector2Scale(enemies[i].velocity, dt));

                            if (CheckCollisionCircles(enemies[i].position, 20, playerPos, 20)) {
                                if (selectedDifficulty == DIFFICULTY_HARD && !secondChanceUsed) {
                                    ResetBowling();
                                    ResetElixirState();
                                    gameState = MINI_GAME;
                                } else {
                                    gameOver = true;
                                    gameState = CLOSING_SCENE;
                                }
                                break;
                            }

                            for (int j = 0; j < MAX_BULLETS; j++) {
                                if (bullets[j].active && CheckCollisionCircles(enemies[i].position, 20, bullets[j].position, 5)) {
                                    enemies[i].active = false;
                                    bullets[j].active = false;
                                    score++;
                                    break;
                                }
                            }
                        }
                    }

                    if (selectedDifficulty == DIFFICULTY_HARD) {
                        Rectangle playerRect = {
                            playerPos.x - pikachuTex.width/2.0f,
                            playerPos.y - pikachuTex.height/2.0f,
                            (float)pikachuTex.width,
                            (float)pikachuTex.height
                        };
                        for (int i = 0; i < MAX_OBSTACLES; i++) {
                            if (obstacles[i].active && CheckCollisionRecs(playerRect, obstacles[i].rect)) {
                                if (!secondChanceUsed) {
                                    ResetBowling();
                                    ResetElixirState();
                                    gameState = MINI_GAME;
                                } else {
                                    gameOver = true;
                                    gameState = CLOSING_SCENE;
                                }
                            }
                        }
                    }
                }
            } break;

            case MINI_GAME: {
                // Toggle strike mode
                if (IsKeyPressed(KEY_S) && !ballLaunched) {
                    strikeMode = !strikeMode;
                    if (strikeMode) {
                        luckyStrike = (GetRandomValue(0, 1) == 1);
                    }
                }

                // Adjust angle
                if (!ballLaunched) {
                    if (IsKeyDown(KEY_LEFT)) throwAngle -= 0.02f;
                    if (IsKeyDown(KEY_RIGHT)) throwAngle += 0.02f;
                    throwAngle = Clamp(throwAngle, -maxAngle, maxAngle);
                    ballPos.x = GetScreenWidth()/2.0f + sinf(throwAngle) * a;
                    ballPos.y = GetScreenHeight() - 80.0f;
                }

                // Power charging
                if (IsKeyDown(KEY_SPACE) && !ballLaunched) {
                    charging = true;
                    power += 0.01f;
                    power = Clamp(power, 0.0f, maxPower);
                }
                if (IsKeyReleased(KEY_SPACE) && charging) {
                    charging = false;
                    ellipseCenter = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight() + 50 };
                    ballLaunched = true;
                    t = 0.0f;
                    ballSpeed = baseSpeed + power * 0.05f;
                }

                // Ball movement (elliptical path)
                if (ballLaunched) {
                    t += ballSpeed;
                    float x = a * cosf(t);
                    float y = b * sinf(t);
                    ballPos.x = ellipseCenter.x + x * cosf(throwAngle) - y * sinf(throwAngle);
                    ballPos.y = ellipseCenter.y - x * sinf(throwAngle) - y * cosf(throwAngle);

                    // Keep ball within lane bounds
                    if (ballPos.x < LANE_LEFT + BALL_RADIUS) ballPos.x = LANE_LEFT + BALL_RADIUS;
                    if (ballPos.x > LANE_RIGHT - BALL_RADIUS) ballPos.x = LANE_RIGHT - BALL_RADIUS;

                    // Collision detection
                    for (int i = 0; i < NUM_PINS; i++) {
                        if (!pins[i].fallen && CheckCollisionCircles(ballPos, BALL_RADIUS, pins[i].position, PIN_RADIUS)) {
                            bool isStrikeCondition = (strikeMode && luckyStrike) || (fabsf(throwAngle) < 0.1f && power > 0.8f);
                            if (isStrikeCondition) {
                                for (int j = 0; j < NUM_PINS; j++) {
                                    pins[j].fallen = true;
                                    pins[j].animating = true;
                                    pins[j].velocity = (Vector2){(float)GetRandomValue(-5, 5), (float)GetRandomValue(5, 10)};
                                    pins[j].rotation = (float)GetRandomValue(0, 360);
                                }
                                if (hitSound.frameCount > 0) PlaySound(hitSound);
                                break;
                            } else {
                                pins[i].fallen = true;
                                pins[i].animating = true;
                                pins[i].velocity = (Vector2){(float)GetRandomValue(-5, 5), (float)GetRandomValue(5, 10)};
                                pins[i].rotation = (float)GetRandomValue(0, 360);
                                if (hitSound.frameCount > 0) PlaySound(hitSound);
                            }
                        }
                    }

                    // Ball leaves lane
                    if (t >= PI / 2) {
                        bool strike = true;
                        for (int i = 0; i < NUM_PINS; i++) {
                            if (!pins[i].fallen) { strike = false; break; }
                        }
                        if (strike) {
                            secondChanceUsed = true;
                            ResetGame(selectedDifficulty);
                            if (selectedDifficulty == DIFFICULTY_HARD) SpawnObstacles();
                            gameState = GAMEPLAY;
                        } else {
                            gameOver = true;
                            gameState = CLOSING_SCENE;
                        }
                        ResetBowling();
                    }
                }

                // Pin animation
                for (int i = 0; i < NUM_PINS; i++) {
                    if (pins[i].animating) {
                        pins[i].position.x += pins[i].velocity.x;
                        pins[i].position.y += pins[i].velocity.y;
                        pins[i].velocity.y += 0.3f;
                        pins[i].rotation += 10.0f;
                        if (pins[i].position.y > GetScreenHeight() + 50.0f) {
                            pins[i].animating = false;
                        }
                    }
                }
            } break;

            case CLOSING_SCENE: {
                if (!animationComplete) {
                    gameOverScale += scaleSpeed * dt;
                    if (gameOverScale >= 1.0f) {
                        gameOverScale = 1.0f;
                        animationComplete = true;
                    }
                }
                if (animationComplete) {
                    if (IsKeyPressed(KEY_R)) {
                        ResetGame(selectedDifficulty);
                        if (selectedDifficulty == DIFFICULTY_HARD) SpawnObstacles();
                        secondChanceUsed = false;
                        gameState = GAMEPLAY;
                    }
                    if (IsKeyPressed(KEY_H)) {
                        ResetGame(selectedDifficulty);
                        secondChanceUsed = false;
                        gameState = OPENING_SCENE;
                    }
                }
            } break;
        }

        // ---------------- DRAW ----------------
        BeginDrawing();
        ClearBackground(gameState == GAMEPLAY ? GREEN : RAYWHITE);

        switch (gameState) {
            case OPENING_SCENE: {
                // Draw logo full screen (cover entire window)
                if (logo.id != 0) {
                    DrawTexturePro(
                        logo,
                        (Rectangle){0, 0, (float)logo.width, (float)logo.height},
                        (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
                        (Vector2){0, 0}, 0.0f, WHITE
                    );
                } else {
                    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), DARKGRAY);
                    DrawText("Logo missing!", GetScreenWidth()/2 - 100, GetScreenHeight()/2, 20, RED);
                }

                // Draw UI elements on top
                DrawTextEx(emojiFont, "Select Game Difficulty", (Vector2){GetScreenWidth()/2 - 160, 250}, 30, 2, DARKGRAY);

                DrawRectangleRec(easyBtn, selectedDifficulty == DIFFICULTY_EASY ? LIME : LIGHTGRAY);
                DrawTextEx(emojiFont, "Easy", (Vector2){easyBtn.x + 70, easyBtn.y + 15}, 20, 2, DARKGRAY);

                DrawRectangleRec(mediumBtn, selectedDifficulty == DIFFICULTY_MEDIUM ? LIME : LIGHTGRAY);
                DrawTextEx(emojiFont, "Medium", (Vector2){mediumBtn.x + 55, mediumBtn.y + 15}, 20, 2, DARKGRAY);

                DrawRectangleRec(hardBtn, selectedDifficulty == DIFFICULTY_HARD ? LIME : LIGHTGRAY);
                DrawTextEx(emojiFont, "Hard", (Vector2){hardBtn.x + 70, hardBtn.y + 15}, 20, 2, DARKGRAY);

                DrawRectangleRec(startBtn, SKYBLUE);
                DrawTextEx(emojiFont, "Start", (Vector2){startBtn.x + 65, startBtn.y + 10}, 30, 2, DARKBLUE);
            } break;

            case GAMEPLAY: {
                DrawTexture(pikachuTex, playerPos.x - pikachuTex.width/2, playerPos.y - pikachuTex.height/2, WHITE);

                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (bullets[i].active) DrawCircleV(bullets[i].position, 5, WHITE);
                }

                for (int i = 0; i < MAX_ENEMIES; i++) {
                    if (enemies[i].active) {
                        DrawTexture(pokeballTex, enemies[i].position.x - pokeballTex.width/2, enemies[i].position.y - pokeballTex.height/2, WHITE);
                    }
                }

                // Draw elixir (100x100 pixels)
                if (elixirAvailable) {
                    if (elixirTex.id != 0) {
                        DrawTexturePro(
                            elixirTex,
                            (Rectangle){0, 0, (float)elixirTex.width, (float)elixirTex.height},
                            (Rectangle){elixirPos.x, elixirPos.y, 100.0f, 100.0f},
                            (Vector2){50.0f, 50.0f}, 0.0f, WHITE
                        );
                    } else {
                        DrawCircleV(elixirPos, 50.0f, PURPLE);
                        DrawText("E", (int)elixirPos.x - 20, (int)elixirPos.y - 24, 40, WHITE);
                    }
                }

                // Show elixir status
                if (elixirReady) {
                    DrawText("Elixir READY! Press S to clear enemies!", 20, 50, 18, YELLOW);
                }

                if (selectedDifficulty == DIFFICULTY_HARD) {
                    for (int i = 0; i < MAX_OBSTACLES; i++) {
                        if (obstacles[i].active) {
                            Rectangle src = {0, 0, (float)obstacleTex.width, (float)obstacleTex.height};
                            Rectangle dst = obstacles[i].rect;
                            DrawTexturePro(obstacleTex, src, dst, (Vector2){0,0}, 0.0f, WHITE);
                        }
                    }
                }

                DrawTextEx(emojiFont, TextFormat("Score: %d", score), (Vector2){20, 20}, 20, 2, BLACK);
            } break;

            case MINI_GAME: {
                if (bowlingBg.id != 0) {
                    DrawTexturePro(bowlingBg,
                                   (Rectangle){0,0,(float)bowlingBg.width,(float)bowlingBg.height},
                                   (Rectangle){0,0,(float)GetScreenWidth(),(float)GetScreenHeight()},
                                   (Vector2){0,0}, 0.0f, WHITE);
                } else {
                    ClearBackground(DARKGREEN);
                    DrawRectangle(LANE_LEFT - 20, 60, (LANE_RIGHT - LANE_LEFT) + 40, GetScreenHeight() - 120, BROWN);
                }

                DrawText("SECOND CHANCE! Score a STRIKE to revive!", 140, 20, 24, YELLOW);
                DrawText("Angle: LEFT/RIGHT | Power: Hold SPACE | S: Strike Mode", 120, 50, 18, RAYWHITE);

                for (int i = 0; i < NUM_PINS; i++) {
                    if (!pins[i].fallen || pins[i].animating) {
                        DrawCircleV(pins[i].position, PIN_RADIUS, WHITE);
                        DrawCircleV(pins[i].position, 8, RED);
                    }
                }

                DrawCircleV(ballPos, BALL_RADIUS, BLUE);

                if (!ballLaunched) {
                    Vector2 guideEnd = {ballPos.x + 50 * sinf(throwAngle), ballPos.y - 50 * cosf(throwAngle)};
                    DrawLineEx(ballPos, guideEnd, 2, strikeMode ? RED : DARKBLUE);
                }

                if (charging) {
                    DrawRectangle(50, GetScreenHeight() - 40, (int)(200 * (power / maxPower)), 20, GREEN);
                    DrawRectangleLines(50, GetScreenHeight() - 40, 200, 20, BLACK);
                }
            } break;

            case CLOSING_SCENE: {
                ClearBackground(BLACK);
                DrawTexture(balhTex, GetScreenWidth()/2 - balhTex.width/2, GetScreenHeight()/2 - balhTex.height - 50, WHITE);
                DrawTextEx(emojiFont, "OOPS THE POKEMON IS CAPTURED!", (Vector2){GetScreenWidth()/2 - 300, GetScreenHeight()/2 - 50}, 40 * gameOverScale, 2, RED);

                if (animationComplete) {
                    DrawTextEx(emojiFont, "Press R to Replay", (Vector2){GetScreenWidth()/2 - 110, GetScreenHeight()/2 + 30}, 20, 2, DARKGRAY);
                    DrawTextEx(emojiFont, "Press H to go to Home Menu", (Vector2){GetScreenWidth()/2 - 160, GetScreenHeight()/2 + 60}, 20, 2, DARKGRAY);
                }
            } break;
        }

        EndDrawing();
    }

    // Cleanup
    UnloadTexture(logo);
    if (emojiFont.texture.id) UnloadFont(emojiFont);
    UnloadTexture(pokeballTex);
    UnloadTexture(pikachuTex);
    UnloadTexture(balhTex);
    if (obstacleTex.id != 0) UnloadTexture(obstacleTex);
    if (bowlingBg.id != 0) UnloadTexture(bowlingBg);
    if (elixirTex.id != 0) UnloadTexture(elixirTex);
    if (hitSound.frameCount > 0) UnloadSound(hitSound);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}