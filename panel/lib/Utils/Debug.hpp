#if DEBUG
    #define PRINT(x)        Serial.print(x)
    #define PRINTLN(x)      Serial.println(x)
#else
    #define PRINT(x)
    #define PRINTLN(x)
#endif

#define PRINTKV(k, v)   PRINT(k); PRINT(": "); PRINTLN(v)
