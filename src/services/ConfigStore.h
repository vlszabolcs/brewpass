#pragma once

#include <Log.h>
#include <Preferences.h>
#include <Types.h>

#include "Settings.h"

// Saves the last valid cloud configuration (price, free mode) to NVS.
// After a restart the device thus works with the usual price even if Firebase is unreachable
// (the original code continued with price = -1 in that case).
class ConfigStore
{
public:
    DeviceConfig load()
    {
        DeviceConfig cfg;
        Preferences prefs;
        if (!prefs.begin(kNamespace, true))
            return cfg;
        if (prefs.isKey("price"))
        {
            cfg.price = prefs.getInt("price", 0);
            cfg.freeMode = prefs.getBool("free", false);
            cfg.loaded = true;
        }
        prefs.end();
        return cfg;
    }

    void save(const DeviceConfig &cfg)
    {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false))
        {
            LOGE("CFG", "Opening NVS failed, configuration not saved");
            return;
        }
        prefs.putInt("price", cfg.price);
        prefs.putBool("free", cfg.freeMode);
        prefs.end();
        LOGI("CFG", "Configuration saved (price=%ld, free=%d)", (long)cfg.price, cfg.freeMode);
    }

    // The last handled app purchase request (see RemoteMark): must survive a reboot, otherwise a request
    // re-delivered right after a restart could be charged twice.
    RemoteMark loadRemoteMark()
    {
        RemoteMark mark;
        Preferences prefs;
        if (!prefs.begin(kNamespace, true))
            return mark;
        if (prefs.isKey("rmTs"))
        {
            mark.tsMs = prefs.getULong64("rmTs", 0);
            prefs.getString("rmId", mark.id, sizeof(mark.id));
        }
        prefs.end();
        return mark;
    }

    void saveRemoteMark(const RemoteMark &mark)
    {
        Preferences prefs;
        if (!prefs.begin(kNamespace, false))
        {
            LOGE("CFG", "Opening NVS failed, remote request mark not saved");
            return;
        }
        prefs.putULong64("rmTs", mark.tsMs);
        prefs.putString("rmId", mark.id);
        prefs.end();
    }

private:
    static constexpr const char *kNamespace = settings::kNvsNamespace;
};
