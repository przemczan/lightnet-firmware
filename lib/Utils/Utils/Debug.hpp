#if DEBUG
    #define PRINT(x)            Serial.print(x)
    #define PRINTLN(x)          Serial.println(x)
#else
    #define PRINT(x)
    #define PRINTLN(x)
#endif

#define PRINT2(x, y)            PRINT(x); PRINT(" "); PRINT(y)
#define PRINT3(x, y, z)         PRINT2(x, y); PRINT(" "); PRINT(z)
#define PRINT4(x, y, z, a)      PRINT3(x, y, z); PRINT(" "); PRINT(a)
#define PRINTLN2(x, y)          PRINT(x); PRINT(" "); PRINTLN(y)
#define PRINTLN3(x, y, z)       PRINT2(x, y); PRINT(" "); PRINTLN(z)
#define PRINTLN4(x, y, z, a)    PRINT3(x, y, z); PRINT(" "); PRINTLN(a)
#define PRINTKV(k, v)           PRINT(k); PRINT(": "); PRINTLN(v)
