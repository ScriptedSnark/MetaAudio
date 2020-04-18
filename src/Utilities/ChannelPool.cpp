#include "Utilities/ChannelPool.hpp"
#include "Vox/VoxManager.hpp"

namespace MetaAudio
{
  ChannelPool::ChannelPool()
  {
    al_xfi_workaround = gEngfuncs.pfnGetCvarPointer("al_xfi_workaround");
  }

  void ChannelPool::SetVox(std::shared_ptr<VoxManager> vox)
  {
    this->vox = vox;
  }

  bool ChannelPool::IsPlaying(sfx_t* sfx)
  {
    auto functor = [&](auto& channel) { return channel.sfx == sfx && channel.source && IsPlaying(channel); };

    return std::any_of(channels.dynamic.begin(), channels.dynamic.end(), functor) ||
           std::any_of(channels.static_.begin(), channels.static_.end(), functor);
  }

  bool ChannelPool::IsPlaying(const aud_channel_t& channel)
  {
    if (channel.source)
    {
      if (al_xfi_workaround->value == 0.0f ||
        al_xfi_workaround->value == 2.0f ||
        channel.source.getLooping() ||
        channel.entchannel == CHAN_STREAM ||
        (channel.entchannel >= CHAN_NETWORKVOICE_BASE && channel.entchannel <= CHAN_NETWORKVOICE_END) ||
        channel.decoder != nullptr ||
        channel.buffer == nullptr)
      {
        return channel.source.isPlaying();
      }
      else
      {
        return channel.source.isPlaying() && std::chrono::steady_clock::now() < channel.playback_end_time;
      }
    }
    return false;
  }

  void ChannelPool::FreeChannel(aud_channel_t* ch)
  {
    if (ch->source)
    {
      // Stop the Source and reset buffer
      ch->buffer = nullptr;
      ch->source.stop();
      ch->source.destroy();
    }

    if (ch->decoder)
    {
      ch->decoder.reset();
    }

    if (ch->isentence >= 0)
    {
      for (size_t i = 0; i < CVOXWORDMAX; ++i)
      {
        vox.lock()->rgrgvoxword[ch->isentence][i].sfx = nullptr;
      }
    }

    ch->isentence = -1;
    ch->sfx = nullptr;

    vox.lock()->CloseMouth(ch);
  }

  aud_channel_t* ChannelPool::SND_PickStaticChannel(int entnum, int entchannel, sfx_t* sfx)
  {
    auto& channel = channels.static_.emplace_back();
    return &channel;
  }

  aud_channel_t* ChannelPool::SND_PickDynamicChannel(int entnum, int entchannel, sfx_t* sfx)
  {
    if (entchannel == CHAN_STREAM && IsPlaying(sfx))
    {
      return nullptr;
    }

    auto& channel = channels.dynamic.emplace_back();
    return &channel;
  }

  void ChannelPool::ClearAllChannels()
  {
    auto functor = [&](auto& channel) { if (channel.sfx != nullptr) FreeChannel(&channel); }; // TODO: move FreeChannel to channel destructor eventually.
    std::for_each(channels.dynamic.begin(), channels.dynamic.end(), functor);
    std::for_each(channels.static_.begin(), channels.static_.end(), functor);

    channels.dynamic.clear();
    channels.static_.clear();
  }

  void ChannelPool::ClearEntityChannels(int entnum, int entchannel)
  {
    auto functor = [&](auto& channel) { if (channel.entnum == entnum && channel.entchannel == entchannel) { FreeChannel(&channel); return true; } return false; };
    channels.dynamic.erase(std::remove_if(channels.dynamic.begin(), channels.dynamic.end(), functor), channels.dynamic.end());
    channels.static_.erase(std::remove_if(channels.static_.begin(), channels.static_.end(), functor), channels.static_.end());
  }

  void ChannelPool::ClearFinished()
  {
    auto functor = [&](aud_channel_t& channel)
    {
      if (channel.isentence < 0 && !IsPlaying(channel))
      {
        FreeChannel(&channel);
        return true;
      }
      return false ;
    };

    channels.dynamic.erase(std::remove_if(channels.dynamic.begin(), channels.dynamic.end(), functor), channels.dynamic.end());
    channels.static_.erase(std::remove_if(channels.static_.begin(), channels.static_.end(), functor), channels.static_.end());
  }

  int ChannelPool::S_AlterChannel(int entnum, int entchannel, sfx_t* sfx, float fvol, float pitch, int flags)
  {
    std::function<bool(aud_channel_t& channel)> functor;

    auto internalFunctor = [&](aud_channel_t& channel)
    {
      if (flags & SND_CHANGE_PITCH)
      {
        channel.pitch = pitch;
        channel.source.setPitch(channel.pitch);
      }

      if (flags & SND_CHANGE_VOL)
      {
        channel.volume = fvol;
        channel.source.setGain(channel.volume);
      }

      if (flags & SND_STOP)
      {
        FreeChannel(&channel);
      }
    };

    if (sfx->name[0] == '!')
    {
      // This is a sentence name.
      // For sentences: assume that the entity is only playing one sentence
      // at a time, so we can just shut off
      // any channel that has ch->isentence >= 0 and matches the
      // soundsource.
      functor = [&](aud_channel_t& channel)
      {
        if (channel.entnum == entnum &&
            channel.entchannel == entchannel &&
            channel.sfx != nullptr &&
            channel.isentence >= 0)
        {
          internalFunctor(channel);
          return true;
        }
        else
        {
          return false;
        }
      };
    }
    else
    {
      functor = [&](aud_channel_t& channel)
      {
        if (channel.entnum == entnum &&
            channel.entchannel == entchannel &&
            channel.sfx == sfx)
        {
          internalFunctor(channel);
          return true;
        }
        else
        {
          return false;
        }
      };
    }

    return std::any_of(channels.dynamic.begin(), channels.dynamic.end(), functor) ||
           std::any_of(channels.static_.begin(), channels.static_.end(), functor);
  }
}