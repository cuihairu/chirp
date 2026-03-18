import React, { useState } from 'react';
import {
  Box, Typography, Paper, TextField, Button, Switch, FormControlLabel,
  Divider, Card, CardContent, Grid, Alert
} from '@mui/material';
import {
  Save as SaveIcon
} from '@mui/icons-material';

interface SettingsSection {
  title: string;
  children: React.ReactNode;
}

const SettingsSection: React.FC<SettingsSection> = ({ title, children }) => (
  <Card sx={{ mb: 3 }}>
    <CardContent>
      <Typography variant="h6" gutterBottom>
        {title}
      </Typography>
      {children}
    </CardContent>
  </Card>
);

const Settings: React.FC = () => {
  const [saved, setSaved] = useState(false);
  const [settings, setSettings] = useState({
    serverName: 'Chirp Server',
    maxUsers: 10000,
    defaultMessageRetention: 30,
    enableRegistration: true,
    requireEmailVerification: true,
    enableVoiceChat: true,
    enableFileUpload: true,
    maxFileSize: 100,
    analyticsEnabled: true,
    debugMode: false,
  });

  const handleChange = (key: string, value: any) => {
    setSettings(prev => ({ ...prev, [key]: value }));
  };

  const handleSave = () => {
    // Save settings to API
    console.log('Saving settings:', settings);
    setSaved(true);
    setTimeout(() => setSaved(false), 3000);
  };

  return (
    <Box>
      <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', mb: 3 }}>
        <Typography variant="h4">Settings</Typography>
        <Button
          variant="contained"
          startIcon={<SaveIcon />}
          onClick={handleSave}
        >
          Save Changes
        </Button>
      </Box>

      {saved && (
        <Alert severity="success" sx={{ mb: 3 }}>
          Settings saved successfully!
        </Alert>
      )}

      <SettingsSection title="General">
        <Grid container spacing={2}>
          <Grid item xs={12} md={6}>
            <TextField
              fullWidth
              label="Server Name"
              value={settings.serverName}
              onChange={(e) => handleChange('serverName', e.target.value)}
            />
          </Grid>
          <Grid item xs={12} md={6}>
            <TextField
              fullWidth
              label="Max Users"
              type="number"
              value={settings.maxUsers}
              onChange={(e) => handleChange('maxUsers', parseInt(e.target.value))}
            />
          </Grid>
          <Grid item xs={12} md={6}>
            <TextField
              fullWidth
              label="Message Retention (days)"
              type="number"
              value={settings.defaultMessageRetention}
              onChange={(e) => handleChange('defaultMessageRetention', parseInt(e.target.value))}
              helperText="Messages older than this will be archived"
            />
          </Grid>
        </Grid>
      </SettingsSection>

      <SettingsSection title="User Registration">
        <Grid container spacing={2}>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.enableRegistration}
                  onChange={(e) => handleChange('enableRegistration', e.target.checked)}
                />
              }
              label="Enable user registration"
            />
          </Grid>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.requireEmailVerification}
                  onChange={(e) => handleChange('requireEmailVerification', e.target.checked)}
                  disabled={!settings.enableRegistration}
                />
              }
              label="Require email verification"
            />
          </Grid>
        </Grid>
      </SettingsSection>

      <SettingsSection title="Features">
        <Grid container spacing={2}>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.enableVoiceChat}
                  onChange={(e) => handleChange('enableVoiceChat', e.target.checked)}
                />
              }
              label="Enable voice chat"
            />
          </Grid>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.enableFileUpload}
                  onChange={(e) => handleChange('enableFileUpload', e.target.checked)}
                />
              }
              label="Enable file upload"
            />
          </Grid>
          {settings.enableFileUpload && (
            <Grid item xs={12} md={6}>
              <TextField
                fullWidth
                label="Max File Size (MB)"
                type="number"
                value={settings.maxFileSize}
                onChange={(e) => handleChange('maxFileSize', parseInt(e.target.value))}
              />
            </Grid>
          )}
        </Grid>
      </SettingsSection>

      <SettingsSection title="Privacy & Analytics">
        <Grid container spacing={2}>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.analyticsEnabled}
                  onChange={(e) => handleChange('analyticsEnabled', e.target.checked)}
                />
              }
              label="Enable analytics and usage tracking"
            />
          </Grid>
        </Grid>
      </SettingsSection>

      <SettingsSection title="Advanced">
        <Grid container spacing={2}>
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  checked={settings.debugMode}
                  onChange={(e) => handleChange('debugMode', e.target.checked)}
                />
              }
              label="Debug mode (increased logging)"
            />
          </Grid>
        </Grid>
      </SettingsSection>
    </Box>
  );
};

export default Settings;
