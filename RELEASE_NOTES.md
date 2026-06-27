# Hyprspace v0.55.4

## Highlights

- Ports Hyprspace to Hyprland `v0.55.4`
- Validated on Hyprland `v0.55.4` under NixOS `26.05 (Yarara)`
- Stabilizes live workspace thumbnails
- Replaces broken mouse binding mechanisms with a self-managed window drag controller in the overview panel
- Prevents window rendering glitches post-overview close by tracking window thumbnail boxes for precise hit-testing

## Compatibility

- Target Hyprland: `v0.55.4`
- Validated under NixOS `26.05 (Yarara)`

## Nix Configuration

Your configuration might look like this:

```nix
{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland/v0.55.4";
    hyprspace = {
      url = "github:RomeoCavazza/hyprspace/v0.55.4";
      inputs.hyprland.follows = "hyprland";
    };
  };
}
```

## Local verification

Recent local verification used:

`nix build /etc/nixos#nixosConfigurations.nixos.config.system.build.toplevel --no-link -L`

Result: passes with the current Hyprspace fork integrated into the NixOS build.
