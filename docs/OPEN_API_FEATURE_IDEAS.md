# Open API Feature Ideas

This note keeps the current working pixel UI baseline intact and ranks small, stable additions that fit the ESP32-P4 local Wi-Fi path.

## Recommended First Build

### 1. Multi-day weather strip

Use the existing Open-Meteo forecast request and expand the daily section from 3 days to 5 days.

Why this is the safest next feature:

- It reuses the API we already know is working.
- Open-Meteo supports daily forecast fields such as weather code, min/max temperature, sunrise, sunset, UV index, precipitation probability, and wind.
- The payload increase is small if we only request 5 daily rows and avoid extra hourly arrays.
- It is highly visual: each day can become a compact pixel forecast tile.

Suggested fields:

```text
daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,wind_speed_10m_max,sunrise,sunset,uv_index_max
forecast_days=5
timezone=Europe/London
```

UI shape:

```text
[Thu] [Fri] [Sat] [Sun] [Mon]
 icon  icon  icon  icon  icon
 9/15  8/14  7/13  8/16  10/17
 20%   60%   45%   10%   30%
```

### 2. Local pixel weather icons

Do not fetch icon images from an API. Store a tiny local icon set and map Open-Meteo weather codes to pixel art.

Icon groups:

- Clear / mostly clear
- Cloudy / overcast
- Fog
- Drizzle
- Rain
- Snow
- Thunder
- Wind

Implementation direction:

- Prefer LVGL canvas or small local bitmap arrays for true pixel icons.
- Keep icons monochrome-plus-accent or 4-colour to match the current dashboard style.
- Render them only when the weather code changes, not every sync tick.

### 3. Better rain timing

Use the hourly precipitation data we already fetch to add a tiny "next rain" line.

Examples:

```text
Rain likely 22:00-01:00
Dry for 6h
Showers from 18:00
```

This is more useful than adding another generic API.

## Good Later Additions

### Air quality

Open-Meteo also has an Air Quality API with European AQI, PM2.5, PM10, pollen, and UV-related fields. This would make a nice small status chip or mini card.

Suggested first version:

```text
current=european_aqi,pm2_5,uv_index
```

Keep this separate from the main weather refresh cadence because air quality updates slower and does not need frequent polling.

### UK bank holiday / civic card

GOV.UK provides a simple JSON endpoint for UK bank holidays. This is compact and useful for a London-focused dashboard.

Possible UI:

```text
Next bank holiday
Spring bank holiday · Mon 25 May
19 days away
```

This can refresh once per day, or even once per boot plus manual refresh.

### TfL disruption focus

The current TfL line card is already useful. The next UI improvement is not more API surface, but a better summary:

- Put disrupted lines first.
- Keep the remaining lines alphabetical.
- Add a small "all clear" banner when there are no disruptions.
- Avoid showing all 20 lines as equal weight if only 1-3 lines matter.

## Not Recommended Yet

- Extra news APIs: most useful news APIs require keys or return large payloads. BBC RSS is already working and lightweight enough.
- Radar tiles: visually tempting, but bitmap/map payloads are too heavy for the current stability envelope.
- Location/IP APIs: not useful because the dashboard is fixed to London.
- Finance/crypto/sports/random APIs: fun, but they distract from the product purpose.

## Implementation Notes

- Keep `esp32p4-local` as the working production baseline.
- Keep new payload caps conservative.
- Avoid adding a fourth frequently polled service until the weather/TfL/news loop has been stable for long sessions.
- Extend existing structs first rather than adding a new transport path.
- Update labels only when text changes, especially once icon redraws are added.
- Use local pixel assets or LVGL primitives for icons; API calls should return data only.

## Useful Sources

- Public APIs catalogue: https://github.com/public-apis/public-apis
- Open-Meteo forecast docs: https://open-meteo.com/en/docs
- Open-Meteo air quality docs: https://open-meteo.com/en/docs/air-quality-api
- GOV.UK bank holidays API: https://www.api.gov.uk/gds/bank-holidays/
- TfL API portal: https://api-portal.tfl.gov.uk/
