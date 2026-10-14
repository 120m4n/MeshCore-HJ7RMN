#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tracecore/Config.h"
#include "tracecore/DutyLimiter.h"
#include "tracecore/Stats.h"
#include "tracecore/Types.h"

namespace tracecore {

constexpr const char* kFwVersion = "0.1.0";

struct Rng {
  virtual ~Rng() = default;
  virtual uint32_t next() = 0;
};

// Recibe líneas completas SIN '\n'. El transporte añade el terminador de forma atómica.
struct LineSink {
  virtual ~LineSink() = default;
  virtual void line(const char* s, size_t n) = 0;
};

// Lo que el cascarón Arduino debe hacer tras cada evento.
struct Action {
  enum Kind : uint8_t {
    None,
    Transmit,     // CAD y, si el canal está libre, TX de bytes/len. Si está ocupado: onChannelBusy()
    StartRx,      // RX continuo; el cascarón lo mantiene tras cada paquete hasta Transmit/Standby
    Standby,      // radio en reposo
    Reconfigure,  // aplicar config().radio y, si estaba escuchando, volver a RX
    Save          // persistir config(); responder con onSaveDone()
  };
  Kind kind = None;
  const uint8_t* bytes = nullptr;  // válido hasta la siguiente llamada al Pinger
  uint8_t len = 0;
};

class Pinger {
 public:
  Pinger(const PingerConfig& cfg, Rng& rng, LineSink& out, uint32_t device_id);

  void emitBoot(bool cfg_reset);  // #HELLO, (#WARN cfg_reset), #CFG
  Action onCommand(std::string_view line, uint32_t now);
  Action onTxDone(uint32_t now, bool ok);
  Action onChannelBusy(uint32_t now);
  Action onRx(const uint8_t* b, size_t n, float rssi, float snr, uint32_t now);
  Action tick(uint32_t now);
  void onSaveDone(bool ok);

  const PingerConfig& config() const { return cfg_; }

 private:
  enum class State : uint8_t { Idle, WaitSend, TxPending, RxWait, Mon };

  struct Pending {
    uint32_t seq = 0, tag = 0;
    uint8_t hash = 0;
  };

  void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void sayRaw(const uint8_t* b, size_t n, int32_t rssi_d, int32_t snr_c, uint32_t now);
  Action act(Action::Kind k) const { Action a; a.kind = k; return a; }

  Action doSet(const char* key, const char* val);
  Action doMon(bool on);
  Action doStop();
  Action startRun(uint16_t count, bool summary, bool go, uint32_t now);
  Action tryBegin(uint32_t now);
  Action finishWith(Status st, uint32_t now);
  Action endRun();
  uint32_t genTag();
  bool inFlight() const { return state_ == State::WaitSend || state_ == State::TxPending || state_ == State::RxWait; }

  PingerConfig cfg_;
  Rng& rng_;
  LineSink& out_;
  uint32_t id_;
  DutyLimiter duty_;
  State state_ = State::Idle;

  // ráfaga / modo continuo
  uint16_t run_total_ = 0, run_done_ = 0, run_ok_ = 0, run_to_ = 0, run_busy_ = 0;
  bool run_go_ = false, run_summary_ = false, stop_req_ = false;
  int32_t up_c_[kMaxBurst], dn_c_[kMaxBurst], rssi_d_[kMaxBurst];
  uint16_t samples_ = 0;

  // ping en curso
  Pending pend_;
  uint32_t seq_ = 0, last_tag_ = 0;
  bool has_last_tag_ = false;
  bool new_ping_ = true, duty_warned_ = false;
  uint8_t cad_tries_ = 0;
  uint32_t next_ms_ = 0, ping_start_ = 0, tx_start_ = 0, tx_dur_ = 0, t_tx_done_ = 0, deadline_ = 0;
  uint8_t tx_buf_[12];
  uint8_t echoes_ = 0;
  int8_t first_snr_up_q_ = 0;
  int32_t first_snr_dn_c_ = 0, first_rssi_d_ = 0;
  uint32_t first_rx_ms_ = 0;

  char buf_[160];
};

}  // namespace tracecore
