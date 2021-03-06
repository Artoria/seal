require 'spec_helper'

describe Reverb do
  describe 'by default' do
    its(:air_absorbtion_hfgain) { should be_within(TOLERANCE).of 0.994 }
    its(:decay_time) { should be_within(TOLERANCE).of 1.49 }
    its(:diffusion) { should be_within(TOLERANCE).of 1.0 }
    its(:density) { should be_within(TOLERANCE).of 1.0 }
    its(:gain) { should be_within(TOLERANCE).of 0.32 }
    its(:hfdecay_limited?) { should be_true }
    its(:hfdecay_ratio) { should be_within(TOLERANCE).of 0.83 }
    its(:hfgain) { should be_within(TOLERANCE).of 0.89 }
    its(:late_delay) { should be_within(TOLERANCE).of 0.011 }
    its(:late_gain) { should be_within(TOLERANCE).of 1.26 }
    its(:reflections_delay) { should be_within(TOLERANCE).of 0.007 }
    its(:reflections_gain) { should be_within(TOLERANCE).of 0.05 }
    its(:room_rolloff_factor) { should be_within(TOLERANCE).of 0 }
  end

  it_validates 'the boolean attribute', :hfdecay_limited
  it_validates 'the float attribute', :air_absorbtion_hfgain, '[0.892, 1]'
  it_validates 'the float attribute', :decay_time, '[0.1, 20]'
  it_validates 'the float attribute', :density, '[0, 1]'
  it_validates 'the float attribute', :diffusion, '[0, 1]'
  it_validates 'the float attribute', :gain, '[0, 1]'
  it_validates 'the float attribute', :hfdecay_ratio, '[0.1, 2]'
  it_validates 'the float attribute', :hfgain, '[0, 1]'
  it_validates 'the float attribute', :late_delay, '[0, 0.1]'
  it_validates 'the float attribute', :late_gain, '[0, 10]'
  it_validates 'the float attribute', :reflections_delay, '[0, 0.3]'
  it_validates 'the float attribute', :reflections_gain, '[0, 3.16]'
  it_validates 'the float attribute', :room_rolloff_factor, '[0, 10]'
  it_defines 'boolean reader aliases', %i(hfdecay_limited)

  specify_preset_loading = -> mod do
    mod.constants.each do |const_sym|
      constant = mod.const_get(const_sym)
      case constant
      when Module
        specify_preset_loading.(constant)
      else
        it "can load preset #{mod}::#{const_sym}" do
          expect do
            reverb = Reverb.new(constant)
            reverb.load(constant)
          end.to_not raise_error
        end
      end
    end
  end
  specify_preset_loading.(Reverb::Preset)
end
