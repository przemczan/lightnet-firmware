#ifdef LIGHTNET_MQTT

#include "MqttBrokerDiscovery.hpp"
#include "../../Utils/Debug.hpp"
#include <mdns.h>
#include <esp_netif_ip_addr.h>
#include <stdio.h>
#include <string.h>

namespace Lightnet {
    namespace {
        bool formatMdnsIpv4(const mdns_ip_addr_t *addrs, char *out, size_t outLen)
        {
            for (const mdns_ip_addr_t *entry = addrs; entry; entry = entry->next) {
                if (entry->addr.type != ESP_IPADDR_TYPE_V4) continue;

                snprintf(out, outLen, IPSTR, IP2STR(&entry->addr.u_addr.ip4));

                return out[0] != '\0';
            }

            return false;
        }
    }
    MqttBrokerDiscovery::MqttBrokerDiscovery()
        : _search(nullptr), _step(QueryStep::None), _mode(MQTT_BROKER_DISCOVERY_MANUAL),
        _state(MqttBrokerDiscoveryState::Idle)
    {
        _endpoint.host[0] = '\0';
        _endpoint.port    = MQTT_PORT_DEFAULT;
        _endpoint.source  = MqttBrokerDiscoverySource::None;
        _endpoint.valid   = false;
    }

    const char * MqttBrokerDiscovery::sourceName(MqttBrokerDiscoverySource source)
    {
        switch (source) {
            case MqttBrokerDiscoverySource::Manual:         return "manual";
            case MqttBrokerDiscoverySource::Mdns:           return "mdns";
            case MqttBrokerDiscoverySource::HomeAssistant:  return "homeassistant";
            case MqttBrokerDiscoverySource::Hassio:         return "hassio";
            default:                                        return "none";
        }
    }

    void MqttBrokerDiscovery::cancelSearch()
    {
        if (_search) {
            mdns_query_async_delete(_search);
            _search = nullptr;
        }
    }

    void MqttBrokerDiscovery::cancel()
    {
        cancelSearch();
        _step  = QueryStep::None;
        _state = MqttBrokerDiscoveryState::Idle;
    }

    bool MqttBrokerDiscovery::allowsMdnsBrowse() const
    {
        return (_mode == MQTT_BROKER_DISCOVERY_AUTO) || (_mode == MQTT_BROKER_DISCOVERY_MDNS);
    }

    bool MqttBrokerDiscovery::allowsHaFallback() const
    {
        return (_mode == MQTT_BROKER_DISCOVERY_AUTO) || (_mode == MQTT_BROKER_DISCOVERY_HA_HOST);
    }

    void MqttBrokerDiscovery::finishSuccess(const char *host, uint16_t port, MqttBrokerDiscoverySource source)
    {
        cancelSearch();
        _step = QueryStep::None;

        strncpy(_endpoint.host, host, sizeof(_endpoint.host) - 1);
        _endpoint.host[sizeof(_endpoint.host) - 1] = '\0';
        _endpoint.port   = port ? port : MQTT_PORT_DEFAULT;
        _endpoint.source = source;
        _endpoint.valid  = true;
        _state           = MqttBrokerDiscoveryState::Done;

        DEBUG_IF(DEBUG_INIT, D_PRINTFLN("[MQTT] broker discovery: %s %s:%u",
                                        sourceName(source), _endpoint.host, (unsigned)_endpoint.port));
    }

    void MqttBrokerDiscovery::finishFailure()
    {
        cancelSearch();
        _step            = QueryStep::None;
        _endpoint.valid  = false;
        _endpoint.source = MqttBrokerDiscoverySource::None;
        _state           = MqttBrokerDiscoveryState::Failed;

        DEBUG_IF(DEBUG_INIT, D_PRINTLN("[MQTT] broker discovery: failed"));
    }

    void MqttBrokerDiscovery::beginStep(QueryStep step)
    {
        cancelSearch();
        _step = step;

        switch (step) {
            case QueryStep::MqttService:
                _search = mdns_query_async_new(nullptr, "_mqtt", "_tcp", MDNS_TYPE_PTR, 3000, 1, nullptr);
                break;

            case QueryStep::HomeAssistant:
                _search = mdns_query_async_new("homeassistant", nullptr, nullptr, MDNS_TYPE_A, 2000, 1, nullptr);
                break;

            case QueryStep::Hassio:
                _search = mdns_query_async_new("hassio", nullptr, nullptr, MDNS_TYPE_A, 2000, 1, nullptr);
                break;

            default:
                break;
        }

        if (!_search) {
            advanceChain();
        }
    }

    void MqttBrokerDiscovery::advanceChain()
    {
        switch (_step) {
            case QueryStep::MqttService:

                if (allowsHaFallback()) {
                    beginStep(QueryStep::HomeAssistant);
                } else {
                    finishFailure();
                }

                break;

            case QueryStep::HomeAssistant:
                beginStep(QueryStep::Hassio);
                break;

            case QueryStep::Hassio:
                finishFailure();
                break;

            default:
                finishFailure();
                break;
        }
    }

    void MqttBrokerDiscovery::pollSearch()
    {
        if (!_search) return;

        mdns_result_t *results = nullptr;

        if (!mdns_query_async_get_results(_search, 0, &results)) {
            return;
        }

        mdns_search_once_s *finished = _search;

        _search = nullptr;
        mdns_query_async_delete(finished);

        if (results) {
            if (_step == QueryStep::MqttService) {
                char hostBuf[MQTT_BROKER_MAX];
                const char *host    = nullptr;
                uint16_t port    = results->port ? results->port : MQTT_PORT_DEFAULT;

                if (formatMdnsIpv4(results->addr, hostBuf, sizeof(hostBuf))) {
                    host = hostBuf;
                } else if (results->hostname && results->hostname[0] != '\0') {
                    host = results->hostname;
                }

                if (host) {
                    mdns_query_results_free(results);
                    finishSuccess(host, port, MqttBrokerDiscoverySource::Mdns);

                    return;
                }
            } else if (_step == QueryStep::HomeAssistant) {
                char hostBuf[MQTT_BROKER_MAX];

                if (formatMdnsIpv4(results->addr, hostBuf, sizeof(hostBuf))) {
                    mdns_query_results_free(results);
                    finishSuccess(hostBuf, MQTT_PORT_DEFAULT,
                                  MqttBrokerDiscoverySource::HomeAssistant);

                    return;
                }
            } else if (_step == QueryStep::Hassio) {
                char hostBuf[MQTT_BROKER_MAX];

                if (formatMdnsIpv4(results->addr, hostBuf, sizeof(hostBuf))) {
                    mdns_query_results_free(results);
                    finishSuccess(hostBuf, MQTT_PORT_DEFAULT, MqttBrokerDiscoverySource::Hassio);

                    return;
                }
            }

            mdns_query_results_free(results);
        }

        advanceChain();
    }

    void MqttBrokerDiscovery::start(uint8_t mode, const char *manualHost, uint16_t manualPort)
    {
        cancel();

        _mode = mode;

        if (manualHost && manualHost[0] != '\0') {
            finishSuccess(manualHost, manualPort, MqttBrokerDiscoverySource::Manual);

            return;
        }

        if (mode == MQTT_BROKER_DISCOVERY_MANUAL) {
            finishFailure();

            return;
        }

        _state = MqttBrokerDiscoveryState::Searching;

        if (allowsMdnsBrowse()) {
            beginStep(QueryStep::MqttService);
        } else if (allowsHaFallback()) {
            beginStep(QueryStep::HomeAssistant);
        } else {
            finishFailure();
        }
    }

    void MqttBrokerDiscovery::tick()
    {
        if (_state != MqttBrokerDiscoveryState::Searching) return;

        pollSearch();
    }
}  // namespace Lightnet

#endif
