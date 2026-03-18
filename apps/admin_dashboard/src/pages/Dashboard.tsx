import React, { useState, useEffect } from 'react';
import {
  Box, Grid, Card, CardContent, Typography, Paper, Table,
  TableBody, TableCell, TableContainer, TableHead, TableRow,
  LinearProgress
} from '@mui/material';
import {
  TrendingUp, TrendingDown, People, Message, NotificationsActive,
  Speed, Storage
} from '@mui/icons-material';
import {
  AreaChart, Area, XAxis, YAxis, CartesianGrid, Tooltip, Legend,
  ResponsiveContainer, LineChart, Line, BarChart, Bar, PieChart, Pie, Cell
} from 'recharts';

interface DashboardProps {
  stats: {
    totalUsers: number;
    onlineUsers: number;
    totalMessages: number;
    activeChannels: number;
  };
}

interface MetricCardProps {
  title: string;
  value: string | number;
  change?: number;
  icon: React.ReactNode;
  color: string;
}

const MetricCard: React.FC<MetricCardProps> = ({ title, value, change, icon, color }) => (
  <Card>
    <CardContent>
      <Box sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Box>
          <Typography color="textSecondary" gutterBottom variant="body2">
            {title}
          </Typography>
          <Typography variant="h4" component="div">
            {value}
          </Typography>
          {change !== undefined && (
            <Box sx={{ display: 'flex', alignItems: 'center', mt: 1 }}>
              {change >= 0 ? (
                <TrendingUp sx={{ color: 'success.main', mr: 0.5, fontSize: 16 }} />
              ) : (
                <TrendingDown sx={{ color: 'error.main', mr: 0.5, fontSize: 16 }} />
              )}
              <Typography
                variant="body2"
                color={change >= 0 ? 'success.main' : 'error.main'}
              >
                {Math.abs(change)}% from last hour
              </Typography>
            </Box>
          )}
        </Box>
        <Box sx={{ p: 1, borderRadius: 1, bgcolor: `${color}.20` }}>
          {icon}
        </Box>
      </Box>
    </CardContent>
  </Card>
);

// Mock data for charts
const generateTimeSeriesData = (points: number, base: number, variance: number) => {
  const data = [];
  const now = Date.now();
  for (let i = points; i >= 0; i--) {
    const timestamp = now - i * 60000; // Every minute
    const value = base + Math.random() * variance - variance / 2;
    data.push({
      time: new Date(timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
      value: Math.floor(value),
    });
  }
  return data;
};

const messageData = generateTimeSeriesData(30, 500, 200);
const userActivityData = generateTimeSeriesData(24, 1000, 300);
const latencyData = generateTimeSeriesData(30, 25, 15);

const channelDistribution = [
  { name: 'Text', value: 65, color: '#6366f1' },
  { name: 'Voice', value: 25, color: '#ec4899' },
  { name: 'Announcement', value: 10, color: '#10b981' },
];

const serviceStatus = [
  { name: 'Gateway', status: 'healthy', uptime: '99.9%', requests: '50.2K' },
  { name: 'Chat', status: 'healthy', uptime: '99.8%', requests: '125.8K' },
  { name: 'Social', status: 'healthy', uptime: '99.9%', requests: '32.1K' },
  { name: 'Voice', status: 'healthy', uptime: '99.7%', requests: '15.4K' },
  { name: 'Auth', status: 'healthy', uptime: '100%', requests: '8.9K' },
  { name: 'Notification', status: 'healthy', uptime: '99.9%', requests: '22.3K' },
];

const recentAlerts = [
  { id: 1, type: 'warning', message: 'High latency on Chat service', time: '2 min ago' },
  { id: 2, type: 'info', message: 'New deployment completed', time: '15 min ago' },
  { id: 3, type: 'error', message: 'Redis connection timeout', time: '1 hour ago' },
];

const Dashboard: React.FC<DashboardProps> = ({ stats }) => {
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    // Simulate loading
    const timer = setTimeout(() => setLoading(false), 500);
    return () => clearTimeout(timer);
  }, []);

  if (loading) {
    return <LinearProgress />;
  }

  return (
    <Box>
      <Typography variant="h4" gutterBottom>
        Dashboard
      </Typography>

      {/* Metric Cards */}
      <Grid container spacing={3} sx={{ mb: 3 }}>
        <Grid item xs={12} sm={6} md={3}>
          <MetricCard
            title="Total Users"
            value={stats.totalUsers.toLocaleString()}
            change={5.2}
            icon={<People sx={{ color: 'primary.main', fontSize: 32 }} />}
            color="primary"
          />
        </Grid>
        <Grid item xs={12} sm={6} md={3}>
          <MetricCard
            title="Online Now"
            value={stats.onlineUsers.toLocaleString()}
            change={12.5}
            icon={<TrendingUp sx={{ color: 'success.main', fontSize: 32 }} />}
            color="success"
          />
        </Grid>
        <Grid item xs={12} sm={6} md={3}>
          <MetricCard
            title="Messages (24h)"
            value={stats.totalMessages.toLocaleString()}
            change={-2.1}
            icon={<Message sx={{ color: 'info.main', fontSize: 32 }} />}
            color="info"
          />
        </Grid>
        <Grid item xs={12} sm={6} md={3}>
          <MetricCard
            title="Active Channels"
            value={stats.activeChannels.toLocaleString()}
            change={8.4}
            icon={<NotificationsActive sx={{ color: 'warning.main', fontSize: 32 }} />}
            color="warning"
          />
        </Grid>
      </Grid>

      {/* Charts */}
      <Grid container spacing={3} sx={{ mb: 3 }}>
        <Grid item xs={12} md={8}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                Messages per Minute
              </Typography>
              <ResponsiveContainer width="100%" height={250}>
                <AreaChart data={messageData}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Area type="monotone" dataKey="value" stroke="#6366f1" fill="#6366f1" fillOpacity={0.3} />
                </AreaChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>
        <Grid item xs={12} md={4}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                Channel Types
              </Typography>
              <ResponsiveContainer width="100%" height={250}>
                <PieChart>
                  <Pie
                    data={channelDistribution}
                    cx="50%"
                    cy="50%"
                    labelLine={false}
                    label={({ name, percent }) => `${name} ${(percent * 100).toFixed(0)}%`}
                    outerRadius={80}
                    fill="#8884d8"
                    dataKey="value"
                  >
                    {channelDistribution.map((entry, index) => (
                      <Cell key={`cell-${index}`} fill={entry.color} />
                    ))}
                  </Pie>
                  <Tooltip />
                </PieChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>
      </Grid>

      <Grid container spacing={3} sx={{ mb: 3 }}>
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                User Activity (24h)
              </Typography>
              <ResponsiveContainer width="100%" height={200}>
                <BarChart data={userActivityData}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Bar dataKey="value" fill="#10b981" />
                </BarChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>
        <Grid item xs={12} md={6}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                API Latency (ms)
              </Typography>
              <ResponsiveContainer width="100%" height={200}>
                <LineChart data={latencyData}>
                  <CartesianGrid strokeDasharray="3 3" />
                  <XAxis dataKey="time" />
                  <YAxis />
                  <Tooltip />
                  <Line type="monotone" dataKey="value" stroke="#ec4899" strokeWidth={2} />
                </LineChart>
              </ResponsiveContainer>
            </CardContent>
          </Card>
        </Grid>
      </Grid>

      {/* Service Status and Alerts */}
      <Grid container spacing={3}>
        <Grid item xs={12} md={8}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                Service Status
              </Typography>
              <TableContainer>
                <Table>
                  <TableHead>
                    <TableRow>
                      <TableCell>Service</TableCell>
                      <TableCell>Status</TableCell>
                      <TableCell align="right">Uptime</TableCell>
                      <TableCell align="right">Requests (24h)</TableCell>
                    </TableRow>
                  </TableHead>
                  <TableBody>
                    {serviceStatus.map((service) => (
                      <TableRow key={service.name}>
                        <TableCell>{service.name}</TableCell>
                        <TableCell>
                          <Box
                            sx={{
                              display: 'inline-block',
                              width: 8,
                              height: 8,
                              borderRadius: '50%',
                              bgcolor: service.status === 'healthy' ? 'success.main' : 'error.main',
                              mr: 1,
                            }}
                          />
                          {service.status}
                        </TableCell>
                        <TableCell align="right">{service.uptime}</TableCell>
                        <TableCell align="right">{service.requests}</TableCell>
                      </TableRow>
                    ))}
                  </TableBody>
                </Table>
              </TableContainer>
            </CardContent>
          </Card>
        </Grid>
        <Grid item xs={12} md={4}>
          <Card>
            <CardContent>
              <Typography variant="h6" gutterBottom>
                Recent Alerts
              </Typography>
              <Box>
                {recentAlerts.map((alert) => (
                  <Box
                    key={alert.id}
                    sx={{
                      py: 1,
                      borderBottom: '1px solid',
                      borderColor: 'divider',
                    }}
                  >
                    <Typography variant="body2" color={alert.type === 'error' ? 'error.main' : 'text.primary'}>
                      {alert.message}
                    </Typography>
                    <Typography variant="caption" color="textSecondary">
                      {alert.time}
                    </Typography>
                  </Box>
                ))}
              </Box>
            </CardContent>
          </Card>
        </Grid>
      </Grid>
    </Box>
  );
};

export default Dashboard;
