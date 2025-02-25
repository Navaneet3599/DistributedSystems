import java.io.*;
import java.net.*;
import java.util.concurrent.*;

public class ClientChatBot
{
	public static final String RESET = "\033[0m";
    public static final String RED = "\033[0;31m";   
    public static final String GREEN = "\033[0;32m";
    public static final String BLUE = "\033[0;34m";
    private static Semaphore mutex = new Semaphore(1);
    private static boolean disconnectRequest = false;
    
    public static class clientHandler implements Runnable
    {
    	private Thread t;
    	private Socket clientSocket;
    	private PrintWriter sendMessage;
    	private BufferedReader readFromConsole;
    	public clientHandler(Socket s)
    	{
    		this.clientSocket = s;
    		try {
    			this.readFromConsole = new BufferedReader(new InputStreamReader(System.in));
    			this.sendMessage = new PrintWriter(this.clientSocket.getOutputStream(), true);
			} catch (IOException e) {
				System.out.println(RED + "Client::Client encountered an error while creating a PrintWriter object..." + RESET);
			}
    	}
    	
    	public void run()
    	{
    		String message = "To exit chat please enter \"78Be1\"";
    		this.sendMessage.println(message);
    		while((message != null) && (!message.equals("78Be1")))
    		{
    			try {
					mutex.acquire();
					if(disconnectRequest == true)
					{
						System.out.println(RED + "Client::Server initiated disconnect request..." + RESET);
	    				break;
					}
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					System.out.println(RED + "Client::Client(write) encountered an error while acquiring the mutex..." + RESET);
				} finally
    			{
					mutex.release();
    			}
    			
    			
    			try {
        			message = this.readFromConsole.readLine();
        			message = GREEN + "Client::" + message + RESET;
    				System.out.println("\033[A\r" + message);
    				this.sendMessage.println(message);
    			} catch (IOException e) {
    				System.out.println(RED + "Client::Client encountered an error while reading from console..." + RESET);
    			}	
    			
    			if((message != null) && (message.equals("78Be1")))
    			{
    				try {
    					mutex.acquire();
    					disconnectRequest = true;
    					break;
    				} catch (InterruptedException e) {
    					// TODO Auto-generated catch block
    					e.printStackTrace();
    				} finally
    				{
    					mutex.release();
    				}
    	    		
    	    		System.out.println(RED + "Client::Client initiated a disconnect request..." + RESET);
    			}
    		}
    		
    		
    		
    		try {
				this.readFromConsole.close();
				this.sendMessage.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				System.out.println(RED + "Client::Client encountered an error while disconnection(writer)..." + RESET);
			}
    	}
    	
    	public void start()
    	{
    		if(t == null)
    		{
    			t = new Thread(this);
    			t.start();
    		}
    	}
    }
    
    public static class serverHandler implements Runnable
    {
    	private Thread t;
    	private Socket clientSocket;
    	private BufferedReader readFromClient;
    	public serverHandler(Socket s)
    	{
    		this.clientSocket = s;
    		try {
				this.readFromClient = new BufferedReader(new InputStreamReader(this.clientSocket.getInputStream()));
			} catch (IOException e) {
				System.out.println(RED + "Client::Client encountered an error while creating a buffered reader for client socket..." + RESET);
			}
    	}
    	
    	public void run()
    	{
    		String message = "";
    		while((message != null)&&(!message.equals("78Be1")))
    		{
    			try {
    				try {
    					mutex.acquire();
						if(disconnectRequest == true)
						{
							disconnectRequest = true;
							System.out.println(RED + "Client::Server initiated disconnect request..." + RESET);
							break;
						}
					} catch (InterruptedException e) {
						// TODO Auto-generated catch block
						System.out.println(RED + "Server::Server(write) encountered an error while acquiring the mutex..." + RESET);
					} finally
    				{
						mutex.release();
    				}
    				
    				message = this.readFromClient.readLine();
    				System.out.println(message);
    				
    				try {
						if((message != null) && (message.equals("78Be1")))
						{
							mutex.acquire();
							disconnectRequest = true;
							
							System.out.println(RED + "Client::Client initiated disconnect request..." + RESET);
						}
					} catch (InterruptedException e) {
						// TODO Auto-generated catch block
						System.out.println(RED + "Client::Client(write) encountered an error while acquiring the mutex..." + RESET);
					} finally
    				{
						mutex.release();
    				}
    				
    			} catch (IOException e) {
    				System.out.println(RED + "Client::Client encountered an error while reading from client socket..." + RESET);
    			}	
    		}

    		
    		try {
				this.readFromClient.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				System.out.println(RED + "Client::Client encountered an error while disconnection(reader)..." + RESET);
			}
    	}
    	
    	public void start()
    	{
    		if(t == null)
    		{
    			t = new Thread(this);
    			t.start();
    		}
    	}
    }
    
	public static void main(String[] args) {
		int serverPort = 50280;
		String serverAddress = "localhost";

		try {
			Socket clientSocket = new Socket(serverAddress, serverPort);

			
			Thread serverThread  = new Thread(new serverHandler(clientSocket));
			Thread clientThread  = new Thread(new clientHandler(clientSocket));
			
			serverThread.start();
            clientThread.start();
            
            try {
				serverThread.join();
				clientThread.join();
			} catch (InterruptedException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
			} finally
            {
				clientSocket.close();
            }
            
			
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}

	}

}
