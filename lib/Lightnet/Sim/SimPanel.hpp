#pragma once
#ifdef SIM_MODE

    #include "../Core/Panel/AnimationPlayer.hpp"
    #include "SimRGBController.hpp"
    #include "../Common/Protocol.hpp"
    #include "../Core/Common/Palette.hpp"

    class SimPanel
    {
        public:
            SimPanel()
            {
            }

            void setIndex(uint8_t idx)
            {
                panelIndex = idx;
                rgb.setPanelIndex(idx);
            }

            uint8_t getIndex() const
            {
                return panelIndex;
            }

            void handlePacket(const Protocol::PacketMeta *packet, uint8_t size);
            void getState(Protocol::PanelState *state) const;
            void tick();

        private:
            uint8_t panelIndex = 0;
            Lightnet::AnimationPlayer player;
            SimRGBController rgb;
    };

#endif  // SIM_MODE
