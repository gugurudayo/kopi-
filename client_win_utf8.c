/*client_win_utf8.c*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include "common_utf8.h"
#include "client_func_utf8.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h> 
#include <stdlib.h> // qsortを使用するために必要

#define BACKGROUND_IMAGE "22823124.jpg" 
#define RESULT_IMAGE "Chatgpt.png"     
#define RESULT_BACK_IMAGE "2535410.jpg" 
#define YELLOW_WEAPON_ICON_IMAGE "1192635.png" // ID 2 用 (黄色)
#define BLUE_WEAPON_ICON_IMAGE "862582.png"   // ID 1 用 (青)
#define RED_WEAPON_ICON_IMAGE "23667746.png"  // ID 0 用 (赤)
#define GREEN_WEAPON_ICON_IMAGE "1499296.png" // ID 3 用 (緑)
#define FONT_PATH "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf"
#define DEFAULT_WINDOW_WIDTH 1300
#define DEFAULT_WINDOW_HEIGHT 1000
#define SCREEN_STATE_LOBBY_WAIT 0
#define SCREEN_STATE_GAME_SCREEN 1
#define SCREEN_STATE_RESULT 2
#define SCREEN_STATE_TITLE 3

// ★ 追加した体力バー描画用の定数 ★
#define HP_BAR_HEIGHT 5 // HPバーの高さ
#define HP_BAR_WIDTH 50 // HPバーの幅 (プレイヤーサイズと同じ)
#define MAX_HP 150      // 最大体力 (タフネス機のHP:150に合わせる)

/* グローバルUI/状態変数 */
static SDL_Window *gMainWindow = NULL;
static SDL_Renderer *gMainRenderer = NULL;
static SDL_Texture *gBackgroundTexture = NULL;
static SDL_Texture *gResultTexture = NULL;
static SDL_Texture *gResultBackTexture = NULL;
static SDL_Texture *gYellowWeaponIconTexture = NULL; // ID 2 用
static SDL_Texture *gBlueWeaponIconTexture = NULL;   // ID 1 用
static SDL_Texture *gRedWeaponIconTexture = NULL;    // ID 0 用
static SDL_Texture *gGreenWeaponIconTexture = NULL;  // ID 3 用
static TTF_Font *gFontLarge = NULL;
static TTF_Font *gFontNormal = NULL;
static TTF_Font *gFontRank = NULL; 
static TTF_Font *gFontName = NULL;
static char gAllClientNames[MAX_CLIENTS][MAX_NAME_SIZE];
static int gClientCount = 0;
int gMyClientID = -1;
static int gXPressedFlags[MAX_CLIENTS] = {0};
static int gCurrentScreenState = SCREEN_STATE_LOBBY_WAIT;
static int gWeaponSent = 0;

static int gPlayerPosX[MAX_CLIENTS];
static int gPlayerPosY[MAX_CLIENTS];

static int gPlayerMoveStep[MAX_CLIENTS]; 

Projectile gProjectiles[MAX_PROJECTILES];

int gPlayerHP[MAX_CLIENTS]; // HP配列

// ステータスID: 0:CT, 1:飛距離, 2:威力, 3:体力, 4:連射数, 5:移動速度
int gWeaponStats[MAX_WEAPONS][MAX_STATS_PER_WEAPON] = {
    // 武器 0: 高速アタッカー (低CT, 高速, 低体力)
    { 500, 1000, 10, 100, 3, 20 },
    // 武器 1: ヘビーシューター (高威力, 長飛距離, 低速)
    { 1500, 1500, 30, 120, 1, 5 },
    // 武器 2: バランス型 (平均的)
    { 1000, 1200, 20, 110, 2, 10 },
    // 武器 3: タフネス機 (高体力, 高連射, 短飛距離)
    { 800, 800, 15, 150, 4, 15 }
};

// ステータス名の定義（表示用）
char gStatNames[MAX_STATS_PER_WEAPON][MAX_STAT_NAME_SIZE] = {
    "クールタイム(ms)", 
    "球の飛距離(px)", 
    "球1つの威力", 
    "プレイヤの体力", 
    "連射可能数", 
    "移動速度(px)"
};

void InitProjectiles(void)
{
    memset(gProjectiles, 0, sizeof(gProjectiles));
}

// 弾の更新と描画を行う関数
void UpdateAndDrawProjectiles(void)
{
    const int SQ_SIZE = 5; // 弾のサイズ (非常に小さい円)

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (gProjectiles[i].active) {
            // 1. 位置の更新（上方向への移動）
            gProjectiles[i].y -= PROJECTILE_STEP;

            // 2. 画面外チェック
            if (gProjectiles[i].y < 0) {
                gProjectiles[i].active = 0; // 画面外に出たら無効化
                continue;
            }
            // 3. 描画
            SDL_Rect bulletRect = { 
                gProjectiles[i].x, 
                gProjectiles[i].y, 
                SQ_SIZE, 
                SQ_SIZE 
            };
            // 弾の色は白に設定
            SDL_SetRenderDrawColor(gMainRenderer, 255, 255, 255, 255);
            SDL_RenderFillRect(gMainRenderer, &bulletRect); // 簡略化のため四角形で描画
        }
    }
}

// 発射コマンドをサーバーに送信する関数
void SendFireCommand(void)
{
    // サーバーに FIRE_COMMAND を送信
    unsigned char data[MAX_DATA];
    int dataSize = 0;
    SetCharData2DataBlock(data, FIRE_COMMAND, &dataSize); 
    // 自分のIDと現在の座標 (発射元の位置) を情報として送信
    SetIntData2DataBlock(data, gMyClientID, &dataSize);
    // プレイヤーの正方形(50px)の中央付近(20pxオフセット)を送信
    SetIntData2DataBlock(data, gPlayerPosX[gMyClientID] + 20, &dataSize); 
    SetIntData2DataBlock(data, gPlayerPosY[gMyClientID], &dataSize);
    SendData(data, dataSize);
}
// SetScreenState は下部で定義されているため、ここで宣言が必要
void SetScreenState(int state);
Uint32 BackToTitleScreen(Uint32 interval, void *param)
{
    printf("[DEBUG] BackToTitleScreen called - post user event\n");
    fflush(stdout);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_USEREVENT;
    ev.user.code = 0;
    ev.user.data1 = (void*)(intptr_t)SCREEN_STATE_TITLE;
    ev.user.data2 = NULL;
    SDL_PushEvent(&ev);
    return 0; 
}

/* プロトタイプ */
void DrawImageAndText(void);
static void DrawText_Internal(const char* text,int x,int y,Uint8 r,Uint8 g,Uint8 b, TTF_Font* font){
    // フォントがロードされていれば描画する
    if (!font || !text) return; 
    SDL_Color color = {r,g,b,255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font,text,color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(gMainRenderer,surf);
    SDL_Rect dst = {x,y,surf->w,surf->h};
    SDL_FreeSurface(surf);
    SDL_RenderCopy(gMainRenderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

// ★ 追記: HPに基づいて順位を計算するロジック ★
// プレイヤーIDとそのHPを保持する構造体
typedef struct {
    int id;
    int hp;
} PlayerScore;

// qsort用の比較関数 (HPが高い順にソート)
int ComparePlayerScores(const void *a, const void *b) {
    const PlayerScore *scoreA = (const PlayerScore *)a;
    const PlayerScore *scoreB = (const PlayerScore *)b;
    // HPが高い方を上位 (降順) にする
    return scoreB->hp - scoreA->hp;
}

// HPに基づいてプレイヤーIDの順位リストを返す関数
void GetRankedPlayerIDs(int *rankedIDs) {
    PlayerScore scores[MAX_CLIENTS];
    int numClients = gClientCount; // グローバル変数 gClientCount を使用

    for (int i = 0; i < numClients; i++) {
        scores[i].id = i;
        scores[i].hp = gPlayerHP[i]; // 現在のHPを使用
    }

    // HPが高い順にソート
    qsort(scores, numClients, sizeof(PlayerScore), ComparePlayerScores);

    // ソートされたIDを結果の配列に格納
    for (int i = 0; i < numClients; i++) {
        rankedIDs[i] = scores[i].id;
    }
}

/* DrawImageAndText: 状態に応じた描画 */
void DrawImageAndText(void){
    int w=800,h=600;
    SDL_GetWindowSize(gMainWindow,&w,&h);
    SDL_RenderClear(gMainRenderer);
    if (gCurrentScreenState == SCREEN_STATE_LOBBY_WAIT){
        // ... (ロビー画面の描画は省略) ...
        if (gBackgroundTexture) SDL_RenderCopy(gMainRenderer,gBackgroundTexture,NULL,NULL);
        else 
        { SDL_SetRenderDrawColor(gMainRenderer,0,100,0,255); SDL_RenderFillRect(gMainRenderer,NULL); 
        }
        int textW=0;
        int titleY = 20; // ★ 修正: titleY の宣言と初期化を追加 ★
        if (gFontLarge) TTF_SizeUTF8(gFontLarge,"Tetra Conflict",&textW,NULL);
        DrawText_Internal("Tetra Conflict",(w-textW)/2,20,255,255,255,gFontLarge);

        const char* promptMsg = "Xキーを押してください";
        int promptW=0;
        int promptY = titleY + 60; // タイトルから下に約60px離して配置
        
        if (gFontName) TTF_SizeUTF8(gFontName, promptMsg, &promptW, NULL); 
        
        DrawText_Internal(promptMsg, (w-promptW)/2, promptY, 255, 200, 0, gFontName); // 黄色系の文字で描画

        // 参加者見出し
        DrawText_Internal("参加者:", (w/2)-100, h/2, 255,255,255, gFontName);
        int baseY = h/2 + 40;
        int lineH = 40; // フォントサイズ大きくしたので行間も広め
        for (int i = 0; i < 4; i++){ // 最大4人
            int nameX = (w/2) - 100;
            int nameY = baseY + i * lineH;
            const char* nameToDraw = (i < gClientCount) ? gAllClientNames[i] : "";
            DrawText_Internal(nameToDraw, nameX, nameY, 255,255,255, gFontName);
            if (i < gClientCount && gXPressedFlags[i] == 1){
                DrawText_Internal(" X Pressed", nameX + 200, nameY, 255,200,0, gFontName);
            }
        }
    }
    else if (gCurrentScreenState == SCREEN_STATE_GAME_SCREEN){
        // 背景描画
        if (gBackgroundTexture) SDL_RenderCopy(gMainRenderer,gBackgroundTexture,NULL,NULL);
        else { SDL_SetRenderDrawColor(gMainRenderer,0,100,0,255); SDL_RenderFillRect(gMainRenderer,NULL); }    
        // 武器4つ描画のための計算
        const int P=20, Y_OFF=50;
        int rectW=(w-P*4)/2;  
        int rectH=(h-P*7)/2;  
        int leftX=(w-(rectW*2+P))/2;
        int rightX=leftX+rectW+P;
        int topY=(h-(rectH*2+P))/2 + Y_OFF;
        int bottomY=topY+rectH+P;
        
        for (int i = 0; i < MAX_WEAPONS; i++) {
            SDL_Rect r;
            Uint8 rColor=0, gColor=0, bColor=0;

            // 座標と色を設定
            if (i == 0) { r.x=leftX; r.y=topY; rColor=255; } // 左上 (赤)
            else if (i == 1) { r.x=rightX; r.y=topY; bColor=255; } // 右上 (青)
            else if (i == 2) { r.x=leftX; r.y=bottomY; rColor=255; gColor=255; } // 左下 (黄)
            else if (i == 3) { r.x=rightX; r.y=bottomY; gColor=255; } // 右下 (緑)
            r.w=rectW; r.h=rectH;
            // 長方形の描画
            SDL_SetRenderDrawColor(gMainRenderer, rColor, gColor, bColor, 255); 
            SDL_RenderFillRect(gMainRenderer,&r);

            // ステータス情報の描画
            int textPadding = 10;
            int lineHeight = 22; // gFontNormal に合わせて行間を調整
            
            for (int j = 0; j < MAX_STATS_PER_WEAPON; j++) {
                char statText[64];
                
                // ステータス名と値を連結
                sprintf(statText, "%s: %d", 
                        gStatNames[j], 
                        gWeaponStats[i][j]);

                // テキスト描画 (長方形の座標 + パディング + 行 * 行間)
                DrawText_Internal(statText, 
                                  r.x + textPadding, 
                                  r.y + textPadding + (j * lineHeight), 
                                  255, 255, 255, gFontNormal); // 白文字で描画
            }
            
            // ★ 追記/修正: アイコンの描画ロジックを統合 ★
            SDL_Texture *iconToDraw = NULL;
            if (i == 0) { // 赤色 (武器ID 0) の場合
                iconToDraw = gRedWeaponIconTexture;
            } else if (i == 1) { // 青色 (武器ID 1) の場合
                iconToDraw = gBlueWeaponIconTexture;
            } else if (i == 2) { // 黄色 (武器ID 2) の場合
                iconToDraw = gYellowWeaponIconTexture;
            } else if (i == 3) { // 緑色 (武器ID 3) の場合
                iconToDraw = gGreenWeaponIconTexture; 
            }

            if (iconToDraw) {
                int iconW = 150; 
                int iconH = 64;  
                int margin = 10; 
                
                SDL_Rect destRect = { 
                    r.x + textPadding, 
                    r.y + r.h - iconH - margin, // 長方形の下端に配置
                    iconW, 
                    iconH 
                }; 
                SDL_RenderCopy(gMainRenderer, iconToDraw, NULL, &destRect);
            }
        }
    }
    else if (gCurrentScreenState == SCREEN_STATE_RESULT){
        // ... (バトル画面の描画は省略)
        if (gResultTexture) SDL_RenderCopy(gMainRenderer,gResultTexture,NULL,NULL);
        else { SDL_SetRenderDrawColor(gMainRenderer,153,204,0,255); SDL_RenderFillRect(gMainRenderer,NULL); }
        
        // プレイヤー正方形の描画ブロック
        const int SQ_SIZE = 50; 
        
        for (int i = 0; i < gClientCount; i++) {
            // プレイヤーが持つ現在の体力
            int currentHP = gPlayerHP[i]; 

            SDL_Rect playerRect;
            playerRect.x = gPlayerPosX[i]; 
            playerRect.y = gPlayerPosY[i]; 
            playerRect.w = SQ_SIZE;
            playerRect.h = SQ_SIZE;

            // プレイヤーを区別するために、ここではクライアントIDに基づいた色を設定
            Uint8 r=0, g=0, b=0;
            if (i == 0) { r=255; g=0; b=0; } // 1人目: 赤
            else if (i == 1) { r=0; g=0; b=255; } // 2人目: 青
            else if (i == 2) { r=255; g=255; b=0; } // 3人目: 黄
            else if (i == 3) { r=0; g=255; b=0; } // 4人目: 緑

            SDL_SetRenderDrawColor(gMainRenderer, r, g, b, 255);
            SDL_RenderFillRect(gMainRenderer, &playerRect);
            
            if (currentHP > 0) {
                char hpText[16];
                sprintf(hpText, "HP: %d", currentHP);
                
                // HPの数値表示位置: プレイヤー正方形の少し上
                int textX = gPlayerPosX[i];
                int textY = gPlayerPosY[i] - 20; // 20px上に表示
                
                Uint8 textR = 255, textG = 255, textB = 255; // デフォルトは白
                
                // HPが低い場合は赤にする
                if (currentHP <= MAX_HP / 4) {
                    textR = 255; textG = 0; textB = 0;
                }

                // DrawText_Internal を使用してHP値を描画
                DrawText_Internal(hpText, 
                                  textX, 
                                  textY, 
                                  textR, textG, textB, 
                                  gFontNormal); // gFontNormal を使用
            }

            // 自分のプレイヤーには枠を描画して強調
            if (i == gMyClientID) {
                SDL_SetRenderDrawColor(gMainRenderer, 255, 255, 255, 255); // 白い枠
                SDL_RenderDrawRect(gMainRenderer, &playerRect);
            }
        }
        UpdateAndDrawProjectiles();
    }
    else if (gCurrentScreenState == SCREEN_STATE_TITLE){
        // ★ 追記: HPに基づいて順位を計算し、プレイヤーIDを取得 ★
        int rankedIDs[MAX_CLIENTS];
        GetRankedPlayerIDs(rankedIDs);
        
        if (gResultBackTexture) SDL_RenderCopy(gMainRenderer,gResultBackTexture,NULL,NULL);
        else { SDL_SetRenderDrawColor(gMainRenderer,0,0,255,255); SDL_RenderFillRect(gMainRenderer,NULL); }
        int titleBottomY = 0;
        if (gFontLarge){
            int textW=0, textH=0;
            TTF_SizeUTF8(gFontLarge,"結果発表!!",&textW,&textH);
            DrawText_Internal("結果発表!!",(w-textW)/2,40,255,255,255,gFontLarge);
            titleBottomY = 40 + textH;
        }
        // 補助テキスト
        int helpTextY = h - 40;
        if (gFontNormal){
            const char* msg = "参加者全員がXボタンを押すと、ゲームが終了します。";
            int textW=0,textH=0;
            TTF_SizeUTF8(gFontNormal,msg,&textW,&textH);
            helpTextY = h - textH - 40;
            DrawText_Internal(msg,(w-textW)/2,helpTextY,0,0,200,gFontNormal);
        }
        // 結果ボックス描画
        int topMargin = 20;
        int bottomMargin = 20;
        int rectCount = gClientCount; // 参加人数分だけ描画
        int spacing = 10;
        int availableHeight = (helpTextY - bottomMargin) - (titleBottomY + topMargin);
        int rectH = (availableHeight - spacing*(rectCount-1)) / rectCount;
        int rectW = 350;
        int startX = (w - rectW)/2;
        SDL_SetRenderDrawColor(gMainRenderer,255,0,0,255);
        SDL_Rect rect;
        for (int i=0; i<rectCount; i++){
            rect.x = startX;
            rect.y = titleBottomY + topMargin + i*(rectH + spacing);
            rect.w = rectW;
            rect.h = rectH;
            SDL_RenderDrawRect(gMainRenderer,&rect);
        }
        // ラベル & 名前
        const char* rankLabels[4] = {"1st:","2nd:","3rd:","4th:"};
        for(int i=0; i<rectCount; i++){
            int rectY = titleBottomY + topMargin + i*(rectH + spacing);
            
            // 順位付けされたプレイヤーの元のクライアントIDを取得
            int clientID = rankedIDs[i];
            
            int textW=0,textH=0;
            TTF_SizeUTF8(gFontRank, rankLabels[i], &textW, &textH);
            int labelX = startX + 15;
            int labelY = rectY + (rectH - textH)/2;
            DrawText_Internal(rankLabels[i], labelX, labelY, 0,0,255,gFontRank);

            // ★ 修正: 順位に基づいて名前とHPを描画 ★
            const char* name = gAllClientNames[clientID];
            int hp = gPlayerHP[clientID];
            
            char nameAndHP[MAX_NAME_SIZE + 10]; 
            sprintf(nameAndHP, "%s (HP: %d)", name, hp); // 名前とHPを表示
            
            int nameW=0,nameH=0;
            TTF_SizeUTF8(gFontName,nameAndHP,&nameW,&nameH);
            int nameX = labelX + textW + 20;
            int nameY = rectY + (rectH - nameH)/2;
            DrawText_Internal(nameAndHP, nameX, nameY, 0,0,255,gFontName);
        }
    }
    SDL_RenderPresent(gMainRenderer);
}


/* GetRenderer (client_command から参照するため) */
SDL_Renderer* GetRenderer(void){ return gMainRenderer; }

/* InitWindows: 初期化 */
int InitWindows(int clientID,int num,char name[][MAX_NAME_SIZE]){
    int windowW = DEFAULT_WINDOW_WIDTH;
    int windowH = DEFAULT_WINDOW_HEIGHT;
    gMyClientID = clientID;
    gClientCount = num;
    for(int i=0;i<num;i++){ strncpy(gAllClientNames[i], name[i], MAX_NAME_SIZE-1); gAllClientNames[i][MAX_NAME_SIZE-1]='\0'; }
    // TTF_Init() を維持
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0 || !(IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG) & IMG_INIT_JPG)) {
        fprintf(stderr,"SDL init failed: %s\n", SDL_GetError()); return -1;
    }
    if (TTF_Init() == -1) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
    }
    // gFontLarge, gFontNormal のロード処理を維持
    gFontLarge = TTF_OpenFont(FONT_PATH, 40);
    gFontNormal = TTF_OpenFont(FONT_PATH, 24);
    gFontRank  = TTF_OpenFont(FONT_PATH, 36);
    gFontName  = TTF_OpenFont(FONT_PATH, 36);
    if (!gFontLarge || !gFontNormal) {
        fprintf(stderr, "Failed to load font: %s\n", TTF_GetError());
    }
    SDL_Surface *bg = IMG_Load(BACKGROUND_IMAGE);
    SDL_Surface *res = IMG_Load(RESULT_IMAGE);
    SDL_Surface *resBack = IMG_Load(RESULT_BACK_IMAGE); 
    SDL_Surface *icon_yellow = IMG_Load(YELLOW_WEAPON_ICON_IMAGE); 
    SDL_Surface *icon_blue = IMG_Load(BLUE_WEAPON_ICON_IMAGE);   
    SDL_Surface *icon_red = IMG_Load(RED_WEAPON_ICON_IMAGE);
    SDL_Surface *icon_green = IMG_Load(GREEN_WEAPON_ICON_IMAGE); // ★ 追記: 緑色用のアイコンをロード ★
    
    const char *myWindowTitle = gAllClientNames[gMyClientID];
    gMainWindow = SDL_CreateWindow(myWindowTitle,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,windowW,windowH,0);

    gMainRenderer = SDL_CreateRenderer(gMainWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (bg) { gBackgroundTexture = SDL_CreateTextureFromSurface(gMainRenderer,bg); SDL_FreeSurface(bg); }
    if (res) { gResultTexture = SDL_CreateTextureFromSurface(gMainRenderer,res); SDL_FreeSurface(res); }
    if (resBack) { gResultBackTexture = SDL_CreateTextureFromSurface(gMainRenderer,resBack); SDL_FreeSurface(resBack); } 
    
    // テクスチャ作成
    if (icon_yellow) { 
        gYellowWeaponIconTexture = SDL_CreateTextureFromSurface(gMainRenderer, icon_yellow);
        SDL_FreeSurface(icon_yellow); 
    } else {
        fprintf(stderr, "Failed to load yellow weapon icon: %s\n", YELLOW_WEAPON_ICON_IMAGE);
    }
    if (icon_blue) { 
        gBlueWeaponIconTexture = SDL_CreateTextureFromSurface(gMainRenderer, icon_blue);
        SDL_FreeSurface(icon_blue);
    } else {
        fprintf(stderr, "Failed to load blue weapon icon: %s\n", BLUE_WEAPON_ICON_IMAGE);
    }
    if (icon_red) { 
        gRedWeaponIconTexture = SDL_CreateTextureFromSurface(gMainRenderer, icon_red);
        SDL_FreeSurface(icon_red);
    } else {
        fprintf(stderr, "Failed to load red weapon icon: %s\n", RED_WEAPON_ICON_IMAGE);
    }
    if (icon_green) { // ★ 追記: 緑色アイコンのテクスチャを作成 ★
        gGreenWeaponIconTexture = SDL_CreateTextureFromSurface(gMainRenderer, icon_green);
        SDL_FreeSurface(icon_green);
    } else {
        fprintf(stderr, "Failed to load green weapon icon: %s\n", GREEN_WEAPON_ICON_IMAGE);
    }
    
    /* 初期画面はロビー待機画面 (SCREEN_STATE_LOBBY_WAIT) に変更 */
    gCurrentScreenState = SCREEN_STATE_LOBBY_WAIT;
    
    // プレイヤーの初期座標設定（画面四隅配置）
    int w = DEFAULT_WINDOW_WIDTH;
    int h = DEFAULT_WINDOW_HEIGHT;
    const int SQ_SIZE = 50; 
    const int PADDING = 50; // 画面端からのマージン
    const int S_SIZE = SQ_SIZE; // プレイヤーのサイズ

    // 参加人数に基づいた初期位置の計算
    for (int i = 0; i < gClientCount; i++) {
        // デフォルトの移動量を設定
        gPlayerMoveStep[i] = 10; 
        
        switch (i) {
            case 0: // 1人目: 左上
                gPlayerPosX[i] = PADDING;
                gPlayerPosY[i] = PADDING;
                break;
            case 1: // 2人目: 右下
                gPlayerPosX[i] = w - PADDING - S_SIZE;
                gPlayerPosY[i] = h - PADDING - S_SIZE;
                break;
            case 2: // 3人目: 右上
                gPlayerPosX[i] = w - PADDING - S_SIZE;
                gPlayerPosY[i] = PADDING;
                break;
            case 3: // 4人目: 左下
                gPlayerPosX[i] = PADDING;
                gPlayerPosY[i] = h - PADDING - S_SIZE;
                break;
        }
        // ★ HPの初期値設定（最も高い体力値に設定しておくか、ここで0にするかは要件次第。
        //    ここでは一旦デフォルト値（最も高い体力150）に設定しておきます）
        gPlayerHP[i] = MAX_HP; 
    }
    InitProjectiles();
    DrawImageAndText();
    return 0;
}

/* DestroyWindow: 終了処理 */
void DestroyWindow(void){
    // TTF_CloseFont を維持
    if (gFontLarge) TTF_CloseFont(gFontLarge);
    if (gFontNormal) TTF_CloseFont(gFontNormal);
    if (gFontRank) TTF_CloseFont(gFontRank); // ★ フォント破棄を追加 ★
    if (gResultTexture) SDL_DestroyTexture(gResultTexture);
    if (gBackgroundTexture) SDL_DestroyTexture(gBackgroundTexture);
    if (gResultBackTexture) SDL_DestroyTexture(gResultBackTexture); // ★ 破棄処理を追加 ★
    if (gYellowWeaponIconTexture) SDL_DestroyTexture(gYellowWeaponIconTexture);
    if (gBlueWeaponIconTexture) SDL_DestroyTexture(gBlueWeaponIconTexture); 
    if (gRedWeaponIconTexture) SDL_DestroyTexture(gRedWeaponIconTexture); 
    if (gGreenWeaponIconTexture) SDL_DestroyTexture(gGreenWeaponIconTexture); // ★ 追記: 4つ目のテクスチャの破棄 ★
    if (gMainRenderer) SDL_DestroyRenderer(gMainRenderer);
    if (gMainWindow) SDL_DestroyWindow(gMainWindow);
    // TTF_Quit を維持
    IMG_Quit(); TTF_Quit(); SDL_Quit();
}

/* WindowEvent: 入力処理 */
void WindowEvent(int num){
    SDL_Event event;
    if (SDL_PollEvent(&event)){
        switch(event.type){
            case SDL_QUIT:
                // ウィンドウの閉じるボタンで終了コマンドを送信
                gXPressedFlags[gMyClientID] = 1;
                DrawImageAndText();
                SendEndCommand();
                break;

	    case SDL_MOUSEBUTTONDOWN:
                if (gCurrentScreenState == SCREEN_STATE_GAME_SCREEN){
                    // 武器選択画面でのマウスクリック処理
                    int x = event.button.x;
                    int y = event.button.y;
                    int w, h;
                    SDL_GetWindowSize(gMainWindow, &w, &h);
                    const int P = 20, Y_OFF = 50;
                    int rectW = (w - P*4)/2;
                    int rectH = (h - P*7)/2;
                    int leftX = (w - (rectW*2 + P))/2;
                    int rightX = leftX + rectW + P;
                    int topY = (h - (rectH*2 + P))/2 + Y_OFF;
                    int bottomY = topY + rectH + P;
                    int selectedID = -1;        
                    // クリックされた位置から武器IDを判定
                    if (x >= leftX && x < leftX + rectW){
                        if (y >= topY && y < topY + rectH) selectedID = 0; // 左上
                        else if (y >= bottomY && y < bottomY + rectH) selectedID = 2; // 左下
                    } else if (x >= rightX && x < rightX + rectW){
                        if (y >= topY && y < topY + rectH) selectedID = 1; // 右上
                        else if (y >= bottomY && y < bottomY + rectH) selectedID = 3; // 右下
                    }
                    if (selectedID != -1 && gWeaponSent == 0){
                        unsigned char data[MAX_DATA];
                        int dataSize = 0;
                        SetCharData2DataBlock(data, SELECT_WEAPON_COMMAND, &dataSize);
                        SetIntData2DataBlock(data, selectedID, &dataSize); // 武器IDを情報として送信
                        SendData(data, dataSize);
                        gWeaponSent = 1; // 選択済みフラグを立てる
                    }
                }
                break;

		case SDL_KEYUP:
    if (gCurrentScreenState == SCREEN_STATE_RESULT) {

        char direction = 0;
        switch (event.key.keysym.sym) {
            case SDLK_UP:    direction = DIR_UP;    break;
            case SDLK_DOWN:  direction = DIR_DOWN;  break;
            case SDLK_LEFT:  direction = DIR_LEFT;  break;
            case SDLK_RIGHT: direction = DIR_RIGHT; break;
        }

        if (direction != 0) {
            unsigned char data[MAX_DATA];
            int dataSize = 0;
            SetCharData2DataBlock(data, MOVE_COMMAND, &dataSize);
            SetCharData2DataBlock(data, MOVE_RELEASE, &dataSize);
            SetCharData2DataBlock(data, direction, &dataSize);
            SendData(data, dataSize);
        }
    }
    break;


// ====== KEYDOWN ======
case SDL_KEYDOWN:

    // ロビーでの X キー処理
    if (event.key.keysym.sym == SDLK_x && gCurrentScreenState == SCREEN_STATE_LOBBY_WAIT){
        if (gXPressedFlags[gMyClientID] == 0) {
            gXPressedFlags[gMyClientID] = 1;
            DrawImageAndText();
            unsigned char data[MAX_DATA];
            int dataSize = 0;
            SetCharData2DataBlock(data, X_COMMAND, &dataSize);
            SetIntData2DataBlock(data, gMyClientID, &dataSize);
            SendData(data, dataSize);
        }
        break;
    }

    if (event.key.keysym.sym == SDLK_m){
        SendEndCommand();
        break;
    }

    // ===== ゲーム中の処理 =====
    if (gCurrentScreenState == SCREEN_STATE_RESULT) {

        // 移動キー押下
        char direction = 0;
        switch (event.key.keysym.sym) {
            case SDLK_UP:    direction = DIR_UP;    break;
            case SDLK_DOWN:  direction = DIR_DOWN;  break;
            case SDLK_LEFT:  direction = DIR_LEFT;  break;
            case SDLK_RIGHT: direction = DIR_RIGHT; break;
        }

        if (direction != 0) {
            unsigned char data[MAX_DATA];
            int dataSize = 0;
            SetCharData2DataBlock(data, MOVE_COMMAND, &dataSize);
            SetCharData2DataBlock(data, MOVE_PRESS, &dataSize);
            SetCharData2DataBlock(data, direction, &dataSize);
            SendData(data, dataSize);
        }

        // スペースで弾
        if (event.key.keysym.sym == SDLK_SPACE) {
            SendFireCommand();
        }
    }
    break;
            case SDL_USEREVENT:
                // タイマーなど別スレッドからの画面切替要求をメインスレッドで実行
                {
                    int reqState = (int)(intptr_t)event.user.data1;
                    printf("[DEBUG] Received USEREVENT -> SetScreenState: %d\n", reqState);
                    fflush(stdout);
                    SetScreenState(reqState);
                }
                break;
        }
    }   
}

void SetPlayerMoveStep(int clientID, int step) 
{
    if (clientID < 0 || clientID >= MAX_CLIENTS) return;
    gPlayerMoveStep[clientID] = step;
}

void SetXPressedFlag(int clientID){
    if (clientID < 0 || clientID >= MAX_CLIENTS) return;
    gXPressedFlags[clientID] = 1;
    DrawImageAndText();
}

void SetScreenState(int state){
     printf("[DEBUG] SetScreenState: state=%d\n", state);
    gCurrentScreenState = state;  
    if (state == SCREEN_STATE_GAME_SCREEN) {
        // ゲーム画面への遷移時、武器選択フラグをリセット
        gWeaponSent = 0;
    } else if (state == SCREEN_STATE_LOBBY_WAIT) {
        // ロビー待機画面への遷移時、全員のX押下状態をリセット
        memset(gXPressedFlags, 0, sizeof(gXPressedFlags));
    }
    else if (state == SCREEN_STATE_RESULT) {
        // 結果画面表示後3秒後に自動遷移は、ここでは行わない
        // 代わりに下記で処理
         printf("[DEBUG] RESULT state - timer will be set\n");
    }   
    DrawImageAndText(); 
    // 結果画面表示時にタイマーを設定
    if (state == SCREEN_STATE_RESULT) {
        SDL_AddTimer(90000, BackToTitleScreen, NULL);//制限時間
    }
}

// プレイヤーの座標を更新するヘルパー関数（境界チェックを追加）
void UpdatePlayerPos(int clientID, char direction)
{
    if (clientID < 0 || clientID >= MAX_CLIENTS) return;

    int step = gPlayerMoveStep[clientID];
    int currentX = gPlayerPosX[clientID];
    int currentY = gPlayerPosY[clientID];
    int newX = currentX;
    int newY = currentY;

    // ウィンドウサイズを取得
    int windowW, windowH;
    SDL_GetWindowSize(gMainWindow, &windowW, &windowH);

    // プレイヤーのサイズ（ここでは 50x50 と仮定）
    const int SQ_SIZE = 50; 
    
    // 1. 移動後の新しい座標を計算
    switch (direction) {
        case DIR_UP:
            newY = currentY - step;
            break;
        case DIR_DOWN:
            newY = currentY + step;
            break;
        case DIR_LEFT:
            newX = currentX - step;
            break;
        case DIR_RIGHT:
            newX = currentX + step;
            break;
    }

    // 2. 境界チェックを行い、座標を制限する
    // X軸のチェック
    if (newX < 0) {
        newX = 0; // 左端 (0) より小さくならないようにする
    } else if (newX > windowW - SQ_SIZE) {
        newX = windowW - SQ_SIZE; // 右端 (ウィンドウ幅 - プレイヤーサイズ) を超えないようにする
    }

    // Y軸のチェック
    if (newY < 0) {
        newY = 0; // 上端 (0) より小さくならないようにする
    } else if (newY > windowH - SQ_SIZE) {
        newY = windowH - SQ_SIZE; // 下端 (ウィンドウ高 - プレイヤーサイズ) を超えないようにする
    }

    // 3. 制限された座標で更新
    gPlayerPosX[clientID] = newX;
    gPlayerPosY[clientID] = newY;
}
