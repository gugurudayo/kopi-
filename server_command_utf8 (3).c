/*server_command_utf8.c*/

#include "server_common_utf8.h"
#include "server_func_utf8.h"
#include <arpa/inet.h>
#include <SDL2/SDL.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
// ★★★ サーバー側ゲーム状態管理構造体 ★★★
#define PROJECTILE_SIZE 5
#define PLAYER_SIZE 50
#define SERVER_PROJECTILE_STEP 20
// ★ 定数定義を追加
#define MAX_WEAPONS 4
#define MAX_STATS_PER_WEAPON 6
#define STAT_DAMAGE 2 

typedef struct {
    int x;
    int y;
    int firedByClientID; // 発射元
    int active;
} ServerProjectile;
// ★★★ サーバー側ゲーム状態管理構造体 ここまで ★★★

/* 状態 */
static char gClientHands[MAX_CLIENTS] = {0};          // 0: 未選択, 1..: 選択済み(weaponID+1)
static int gHandsCount = 0;
static int gXPressedClientFlags[MAX_CLIENTS] = {0};   // X押下フラグ（サーバー側保持）
static int gXPressedCount = 0;

static int gActiveProjectileCount = 0; // 現在アクティブな発射体の総数
static ServerProjectile gServerProjectiles[MAX_PROJECTILES]; // サーバーが管理する弾のリスト

static void SetIntData2DataBlock(void *data,int intData,int *dataSize);
static void SetCharData2DataBlock(void *data,char charData,int *dataSize);

// ★ 追記: サーバー側で武器情報を保持するための配列
static int gClientWeaponID[MAX_CLIENTS]; 
static int gServerInitialized = 0; // 初期化フラグ

//
static int gKeyState[MAX_CLIENTS][4] = {0};
static char gMoveDir[MAX_CLIENTS] = {0};   // ←押されてる方向（0 なら停止）
//

int gServerWeaponStats[MAX_WEAPONS][MAX_STATS_PER_WEAPON] = {
    // 武器 0: 高速アタッカー (ダメージ10)
    { 500, 1000, 10, 100, 3, 20 },
    // 武器 1: ヘビーシューター (ダメージ30)
    { 1500, 1500, 30, 120, 1, 5 },
    // 武器 2: バランス型 (ダメージ20)
    { 1000, 1200, 20, 110, 2, 10 },
    // 武器 3: タフネス機 (ダメージ15)
    { 800, 800, 15, 150, 4, 15 }
};

int gPlayerPosX[MAX_CLIENTS] = {0}; 
int gPlayerPosY[MAX_CLIENTS] = {0};

extern CLIENT gClients[MAX_CLIENTS];
extern int GetClientNum(void);

extern int gPlayerPosX[MAX_CLIENTS];
extern int gPlayerPosY[MAX_CLIENTS];

static int CheckCollision(ServerProjectile *bullet, int playerID) {
    // プレイヤーの矩形
    int px = gPlayerPosX[playerID];
    int py = gPlayerPosY[playerID];
    int pw = PLAYER_SIZE;
    int ph = PLAYER_SIZE;

    // 弾の矩形
    int bx = bullet->x;
    int by = bullet->y;
    int bw = PROJECTILE_SIZE;
    int bh = PROJECTILE_SIZE;

    // 矩形同士の衝突判定 (AABB)
    return (bx < px + pw &&
            bx + bw > px &&
            by < py + ph &&
            by + bh > py);
}

// サーバー側で全ての弾を動かし、衝突判定を行うタイマーコールバック
static Uint32 ServerGameLoop(Uint32 interval, void *param) 
{
    int numClients = GetClientNum();

    // ★★★ 追加：キー押下状態に応じて毎フレーム移動 ★★★
    for (int id = 0; id < numClients; id++) {

        int speed = 10; // 移動速度（必要なら武器ステータスで変更）

        if (gKeyState[id][DIR_UP])    gPlayerPosY[id] -= speed;
        if (gKeyState[id][DIR_DOWN])  gPlayerPosY[id] += speed;
        if (gKeyState[id][DIR_LEFT])  gPlayerPosX[id] -= speed;
        if (gKeyState[id][DIR_RIGHT]) gPlayerPosX[id] += speed;

        // ★ 位置をクライアントに送信
        unsigned char data[MAX_DATA];
        int dataSize = 0;
        SetCharData2DataBlock(data, UPDATE_MOVE_COMMAND, &dataSize);
        SetIntData2DataBlock(data, id, &dataSize);
        SetIntData2DataBlock(data, gPlayerPosX[id], &dataSize);
        SetIntData2DataBlock(data, gPlayerPosY[id], &dataSize);
        SendData(ALL_CLIENTS, data, dataSize);
    }
    //
    
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (gServerProjectiles[i].active) {
            
            int shooterID = gServerProjectiles[i].firedByClientID;
            int weaponID = gClientWeaponID[shooterID];
            int attackDamage = 0;
            
            // ★ 修正: 武器IDが有効な場合にのみ攻撃力を取得
            if (weaponID >= 0 && weaponID < MAX_WEAPONS) {
                attackDamage = gServerWeaponStats[weaponID][STAT_DAMAGE];
            } else {
                // 武器未選択時のフォールバック (ここでは最も弱いダメージ10とする)
                attackDamage = 10; 
            }
for (int i = 0; i < MAX_CLIENTS; i++) {
    if (gMoveDir[i] == 0) continue; // 何も押されていない

    int step = 10;
    if (gMoveDir[i] == DIR_UP)    gPlayerPosY[i] -= step;
    if (gMoveDir[i] == DIR_DOWN)  gPlayerPosY[i] += step;
    if (gMoveDir[i] == DIR_LEFT)  gPlayerPosX[i] -= step;
    if (gMoveDir[i] == DIR_RIGHT) gPlayerPosX[i] += step;

    // 移動したら全クライアントへ通知
    unsigned char data[MAX_DATA];
    int dataSize = 0;
    SetCharData2DataBlock(data, UPDATE_MOVE_COMMAND, &dataSize);
    SetIntData2DataBlock(data, i, &dataSize);
    SetCharData2DataBlock(data, gMoveDir[i], &dataSize);
    SendData(ALL_CLIENTS, data, dataSize);
}
            // 弾を移動させる
            gServerProjectiles[i].y -= SERVER_PROJECTILE_STEP;

            // 画面外チェック (ここでは簡易的にy<0)
            if (gServerProjectiles[i].y < 0) {
                gServerProjectiles[i].active = 0;
                gActiveProjectileCount--;
                continue;
            }
            // 衝突判定
            for (int j = 0; j < numClients; j++) {
                // 発射元と自分自身は判定しない
                if (j == gServerProjectiles[i].firedByClientID) continue;

                if (CheckCollision(&gServerProjectiles[i], j)) {
                    // 衝突発生！ターミナルに表示
                    printf(">> HIT! Player %d (%s) was hit by Player %d (%s). Damage: %d\n",
                           j, gClients[j].name,
                           gServerProjectiles[i].firedByClientID, gClients[gServerProjectiles[i].firedByClientID].name,
                           attackDamage); // ダメージ値も出力
                           
                    // 弾を非アクティブにする
                    gServerProjectiles[i].active = 0;
                    gActiveProjectileCount--;
                    
                    // ★ 追記: クライアントに衝突情報を通知するコマンド送信ロジック
                    unsigned char data[MAX_DATA];
                    int dataSize = 0;
                    SetCharData2DataBlock(data, APPLY_DAMAGE_COMMAND, &dataSize);
                    SetIntData2DataBlock(data, j, &dataSize);           // 被弾したクライアントID (j)
                    SetIntData2DataBlock(data, attackDamage, &dataSize); // 武器攻撃力を使用
                    
                    SendData(ALL_CLIENTS, data, dataSize);

                    break; 
                }
            }
        }
    }
    
    // 継続してタイマーを呼び出す
    return interval; 
}

typedef struct {
    unsigned char cmd;  // 送信コマンド
} TimerParam;

/* タイマーコールバック（3秒後に全クライアントへ送信） */
static Uint32 SendCommandAfterDelay(Uint32 interval, void *param)
{
    TimerParam *p = (TimerParam*)param;
    unsigned char data[MAX_DATA];
    int dataSize = 0;
    SetCharData2DataBlock(data, p->cmd, &dataSize);
    SendData(ALL_CLIENTS, data, dataSize);
    free(param);
    printf("[SERVER] Sent command 0x%02X after 3 seconds delay\n", p->cmd);
    return 0; // 一度だけ
}

int ExecuteCommand(char command,int pos)
{
    // ★ 追記: サーバー起動時の一回のみ初期化処理を実行
    if (gServerInitialized == 0) {
        // 全要素を -1 で初期化
        for (int i = 0; i < MAX_CLIENTS; i++) {
            gClientWeaponID[i] = -1;
        }
        gServerInitialized = 1;
    }

    unsigned char data[MAX_DATA];
    int dataSize;
    int endFlag = 1;
    assert(0 <= pos && pos < MAX_CLIENTS);
    switch(command){
        case END_COMMAND:
            dataSize = 0;
            SetCharData2DataBlock(data, END_COMMAND, &dataSize);
            SendData(ALL_CLIENTS, data, dataSize);
            endFlag = 0;
            break;
        case X_COMMAND:
        {
            // クライアントが X 押下を送ってきた（posは送信元）
            int senderID;
            RecvIntData(pos, &senderID);
            if (senderID < 0 || senderID >= GetClientNum()) 
                break;
            if (gXPressedClientFlags[senderID] == 0) {
                gXPressedClientFlags[senderID] = 1;
                gXPressedCount++;
            }
            // 全クライアントに「誰が押したか」を通知（UPDATE_X_COMMAND）
            dataSize = 0;
            SetCharData2DataBlock(data, UPDATE_X_COMMAND, &dataSize);
            SetIntData2DataBlock(data, senderID, &dataSize);
            SendData(ALL_CLIENTS, data, dataSize);
            // もし全員押していたら => 3秒後に START_GAME_COMMAND を送信
            if (gXPressedCount == GetClientNum()) {
                TimerParam *tparam = malloc(sizeof(TimerParam));
                tparam->cmd = START_GAME_COMMAND;
                SDL_AddTimer(3000, SendCommandAfterDelay, tparam);
                // サーバー側フラグをリセット（次ラウンド用）
                gXPressedCount = 0;
                memset(gXPressedClientFlags, 0, sizeof(gXPressedClientFlags));
            }
            break;
        }
        case SELECT_WEAPON_COMMAND:
        {
            int senderID = pos;
            int selectedWeaponID;
            RecvIntData(senderID, &selectedWeaponID);
            if (senderID < 0 || senderID >= GetClientNum()) 
                break;

            if (gClientHands[senderID] == 0) {
                gClientHands[senderID] = (char)(selectedWeaponID + 1);
                gClientWeaponID[senderID] = selectedWeaponID; // ★ 武器IDを記録 ★
                gHandsCount++;
            }
            // 全員選択で 3秒後に NEXT_SCREEN_COMMAND を送信
            if (gHandsCount == GetClientNum()) {
                TimerParam *tparam = malloc(sizeof(TimerParam));
                tparam->cmd = NEXT_SCREEN_COMMAND;
                SDL_AddTimer(3000, SendCommandAfterDelay, tparam);
                gHandsCount = 0;
                memset(gClientHands, 0, sizeof(gClientHands));
            }
            break;
        }
        case MOVE_COMMAND:
        {/*
            int senderID = pos;
            char direction;
            RecvCharData(senderID, &direction);
            
            int step = 10; // 仮の速度。厳密にはクライアントが選択した速度を使うべき。
            
            // 簡易的にクライアント側のロジックを再現
            if (direction == DIR_UP) gPlayerPosY[senderID] -= step;
            else if (direction == DIR_DOWN) gPlayerPosY[senderID] += step;
            else if (direction == DIR_LEFT) gPlayerPosX[senderID] -= step;
            else if (direction == DIR_RIGHT) gPlayerPosX[senderID] += step;
            
            // 移動情報を全クライアントに配信
            dataSize = 0;
            SetCharData2DataBlock(data, UPDATE_MOVE_COMMAND, &dataSize);
            SetIntData2DataBlock(data, senderID, &dataSize);
            SetCharData2DataBlock(data, direction, &dataSize);
            SendData(ALL_CLIENTS, data, dataSize);
            break;*/

		//
	char pressType;     // MOVE_PRESS or MOVE_RELEASE
    char direction;
    RecvCharData(pos, &pressType);
    RecvCharData(pos, &direction);

    if (pressType == MOVE_PRESS) {
        gMoveDir[pos] = direction;     // ←押された方向を記憶
    }
    else if (pressType == MOVE_RELEASE) {
        if (gMoveDir[pos] == direction)
            gMoveDir[pos] = 0;         // ←離したら停止
    }
    break;
    //
        }
        case FIRE_COMMAND: 
        {
            int clientID, x, y;
            
            // 1. クライアントから送信された発射体情報を読み取り
            RecvIntData(pos, &clientID); // 発射元ID
            RecvIntData(pos, &x);        // 初期X座標
            RecvIntData(pos, &y);        // 初期Y座標
            
            // サーバー側での弾の管理（リストに追加）
            for (int i = 0; i < MAX_PROJECTILES; i++) {
                if (!gServerProjectiles[i].active) {
                    gServerProjectiles[i].active = 1;
                    gServerProjectiles[i].firedByClientID = clientID;
                    gServerProjectiles[i].x = x;
                    gServerProjectiles[i].y = y;
                    gActiveProjectileCount++;
                    break;
                }
            }
            
            // ゲームループがまだ動いていなければ起動
            static int timerInitialized = 0;
            if (!timerInitialized) {
                // 1000/60 fps 程度の頻度でゲームループを呼び出す
                SDL_AddTimer(1000 / 60, ServerGameLoop, NULL); 
                timerInitialized = 1;
            }

            printf("[SERVER] Client %d fired! Active projectiles: %d\n", 
                   clientID, 
                   gActiveProjectileCount);
                   
            // 2. 読み取った情報を UPDATE_PROJECTILE_COMMAND として全クライアントにブロードキャスト
            dataSize = 0;
            SetCharData2DataBlock(data, UPDATE_PROJECTILE_COMMAND, &dataSize); // コマンド
            SetIntData2DataBlock(data, clientID, &dataSize);                   // 発射元ID
            SetIntData2DataBlock(data, x, &dataSize);                          // 初期X座標
            SetIntData2DataBlock(data, y, &dataSize);                          // 初期Y座標
            
            SendData(ALL_CLIENTS, data, dataSize);
            
            break;
        }
        default:
            fprintf(stderr,"Unknown command: 0x%02x\n", (unsigned char)command);
            break;
    }
    return endFlag;
}
static void SetIntData2DataBlock(void *data,int intData,int *dataSize)
{
    int tmp;
    assert(data!=NULL);
    assert(0 <= (*dataSize));
    tmp = htonl(intData);
    memcpy((char*)data + (*dataSize), &tmp, sizeof(int));
    (*dataSize) += sizeof(int);
}

static void SetCharData2DataBlock(void *data,char charData,int *dataSize)
{
    assert(data!=NULL);
    assert(0 <= (*dataSize));
    *(char *)((char*)data + (*dataSize)) = charData;
    (*dataSize) += sizeof(char);
}
